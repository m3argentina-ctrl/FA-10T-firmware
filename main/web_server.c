#include "web_server.h"
#include "app_config.h"

#if WEB_SERVER_ENABLED

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_server.h"

#include "app_state.h"
#include "telemetry.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";

static httpd_handle_t s_server;

// Página del dashboard embebida en flash (ver CMakeLists EMBED_TXTFILES). El
// EMBED_TXTFILES agrega un '\0' final, así que se sirve con USE_STRLEN.
extern const char dashboard_html_start[] asm("_binary_dashboard_html_start");

// Escapa una cadena para incrustarla como valor JSON entre comillas. Copia hasta
// dst_sz-1 chars; descarta caracteres de control que romperían el JSON.
static void json_escape(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dst_sz; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n')        { dst[j++] = '\\'; dst[j++] = 'n';     }
        else if (c >= 0x20)        { dst[j++] = (char)c;                  }
        /* otros caracteres de control: se descartan */
    }
    dst[j] = '\0';
}

// GET / → HTML del dashboard.
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, dashboard_html_start, HTTPD_RESP_USE_STRLEN);
}

// GET /api/status → snapshot JSON del estado en vivo + telemetría.
static esp_err_t status_get_handler(httpd_req_t *req)
{
    app_state_lock();
    const app_state_t s = *app_state_get();
    app_state_unlock();

    telemetry_snapshot_t t;
    telemetry_get_snapshot(&t);

    char ip[20] = {0};
    wifi_manager_get_ip(ip, sizeof(ip));
    const char *ssid = wifi_manager_get_ssid();

    char modelo[2 * MODEL_NAME_MAX], serie[2 * SERIE_NUM_MAX];
    char prog[2 * PROG_NAME_MAX],    ssid_e[2 * 33];
    json_escape(modelo, sizeof(modelo), s.modelo);
    json_escape(serie,  sizeof(serie),  s.serie);
    json_escape(prog,   sizeof(prog),   s.nombre_programa);
    json_escape(ssid_e, sizeof(ssid_e), ssid ? ssid : "");

    char buf[1280];
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"temp\":%.1f,\"raw_temp\":%.1f,\"fault\":%d,"
        "\"sp_eff\":%.1f,\"sp_cfg\":%.1f,"
        "\"drv\":%.0f,\"fan\":%.0f,\"aux\":%.0f,"
        "\"hum\":%.1f,\"hum_fault\":%d,"
        "\"op_mode\":%d,\"run_state\":%d,\"warmup\":%d,\"etapa\":%u,"
        "\"elapsed_s\":%lu,\"total_s\":%lu,\"remaining_s\":%lu,"
        "\"t_min\":%.1f,\"t_max\":%.1f,"
        "\"prog\":\"%s\",\"modelo\":\"%s\",\"serie\":\"%s\","
        "\"uptime_s\":%lu,"
        "\"hours_total\":%.1f,\"hours_svc\":%.1f,"
        "\"sessions\":%lu,\"sess_ok\":%lu,\"sess_int\":%lu,"
        "\"fan_faults\":%lu,\"power_fails\":%lu,\"t_max_hist\":%.1f,"
        "\"ssid\":\"%s\",\"ip\":\"%s\""
        "}",
        s.last_sample.temperature, s.last_sample.raw_temperature,
        s.last_sample.fault ? 1 : 0,
        s.effective_setpoint, s.setpoint,
        s.ssr_drv_duty * 100.0f, s.ssr_fan_duty * 100.0f, s.ssr_aux_duty * 100.0f,
        s.humidity, s.humidity_fault ? 1 : 0,
        (int)s.op_mode, (int)s.run_state, s.warmup_done ? 1 : 0,
        (unsigned)s.etapa_activa,
        (unsigned long)s.session_elapsed_s, (unsigned long)s.session_total_s,
        (unsigned long)s.session_remaining_s,
        s.t_min_sesion, s.t_max_sesion,
        prog, modelo, serie,
        (unsigned long)s.uptime_s,
        t.hours_total_s / 3600.0f, t.hours_service_s / 3600.0f,
        (unsigned long)t.sessions_total, (unsigned long)t.sessions_completed,
        (unsigned long)t.sessions_interrupted,
        (unsigned long)t.fan_fault_count, (unsigned long)t.power_fail_count,
        t.t_max_historica,
        ssid_e, ip);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (n < 0 || n >= (int)sizeof(buf)) {  // truncado: enviar lo que haya, válido igual
        return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, buf, n);
}

// GET /api/events → array JSON con el log de eventos (más reciente primero).
static esp_err_t events_get_handler(httpd_req_t *req)
{
    telem_event_t ev[TELEM_EVENT_LOG_DEPTH];
    int n = TELEM_EVENT_LOG_DEPTH;
    telemetry_get_events(ev, &n);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");

    char msg[2 * TELEM_EVENT_MSG_MAX];
    char item[160];
    for (int i = 0; i < n; ++i) {
        json_escape(msg, sizeof(msg), ev[i].msg);
        snprintf(item, sizeof(item),
                 "%s{\"t\":%lu,\"kind\":%d,\"msg\":\"%s\"}",
                 i ? "," : "",
                 (unsigned long)ev[i].timestamp_uptime_s,
                 (int)ev[i].kind, msg);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);   // cierra la respuesta chunked
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.lru_purge_enable = true;          // recicla sockets si se llenan
    config.max_uri_handlers = 8;
    config.stack_size       = 5120;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start falló: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    static const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    static const httpd_uri_t status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
    static const httpd_uri_t events = {
        .uri = "/api/events", .method = HTTP_GET, .handler = events_get_handler };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &status);
    httpd_register_uri_handler(s_server, &events);

    ESP_LOGI(TAG, "servidor web HTTP escuchando en :80 (dashboard de monitoreo)");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

#else  /* WEB_SERVER_ENABLED == 0 — stubs no-op */

esp_err_t web_server_start(void) { return ESP_OK; }
void      web_server_stop(void)  { }

#endif
