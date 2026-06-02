#include "cloud_telemetry.h"
#include "app_config.h"

#if CLOUD_TELEMETRY_ENABLED

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"   // xTaskCreatePinnedToCoreWithCaps / vTaskDeleteWithCaps

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"           // SNTP: hora valida p/ validar cert TLS
#include "nvs_flash.h"
#include "nvs.h"

#include "app_state.h"
#include "telemetry.h"
#include "wifi_manager.h"

// Fallback de compilación para la identidad (banco/desarrollo). El archivo real
// device_identity.h está gitignored; si no existe, los defaults quedan vacíos y
// el módulo se mantiene inerte hasta que haya identidad en la partición NVS.
#if defined(__has_include)
#  if __has_include("device_identity.h")
#    include "device_identity.h"
#  endif
#endif
#ifndef DEVICE_ID_DEFAULT
#  define DEVICE_ID_DEFAULT       ""
#endif
#ifndef DEVICE_TOKEN_DEFAULT
#  define DEVICE_TOKEN_DEFAULT    ""
#endif
#ifndef CLOUD_BASE_URL_DEFAULT
#  define CLOUD_BASE_URL_DEFAULT  ""
#endif

static const char *TAG = "cloud_tel";

// Identidad de fábrica: partición NVS dedicada (sobrevive a borrar config/WiFi).
#define FACTORY_PART  "factory"
#define FACTORY_NS    "fa10t_cloud"

#define CLOUD_INGEST_PATH  "/api/telemetry/ingest"

static char s_dev_id[40];
static char s_token[128];
static char s_base_url[128];

static TaskHandle_t s_task;
static bool         s_provisioned;

// --- Identidad --------------------------------------------------------------
static bool load_identity(void)
{
    s_dev_id[0] = s_token[0] = s_base_url[0] = '\0';

    esp_err_t err = nvs_flash_init_partition(FACTORY_PART);
    if (err == ESP_OK) {
        nvs_handle_t h;
        if (nvs_open_from_partition(FACTORY_PART, FACTORY_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t n;
            n = sizeof s_dev_id;   nvs_get_str(h, "dev_id",    s_dev_id,   &n);
            n = sizeof s_token;    nvs_get_str(h, "token",     s_token,    &n);
            n = sizeof s_base_url; nvs_get_str(h, "cloud_url", s_base_url, &n);
            nvs_close(h);
            ESP_LOGI(TAG, "identidad leída de la partición '%s'", FACTORY_PART);
        }
    } else {
        // ESP_ERR_NOT_FOUND (sin partición) o NO_FREE_PAGES (en blanco) → fallback.
        ESP_LOGW(TAG, "partición '%s' no provista (%s) — uso defaults de compilación",
                 FACTORY_PART, esp_err_to_name(err));
    }

    if (s_dev_id[0]   == '\0') strlcpy(s_dev_id,   DEVICE_ID_DEFAULT,      sizeof s_dev_id);
    if (s_token[0]    == '\0') strlcpy(s_token,    DEVICE_TOKEN_DEFAULT,   sizeof s_token);
    if (s_base_url[0] == '\0') strlcpy(s_base_url, CLOUD_BASE_URL_DEFAULT, sizeof s_base_url);

    bool ok = s_dev_id[0] && s_token[0] && s_base_url[0];
    if (ok) ESP_LOGI(TAG, "telemetría nube: id=%s url=%s", s_dev_id, s_base_url);
    else    ESP_LOGW(TAG, "sin identidad válida — telemetría nube inerte");
    return ok;
}

// --- JSON -------------------------------------------------------------------
// Escapa una cadena para incrustarla como valor JSON entre comillas.
static void json_escape(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dst_sz; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n')        { dst[j++] = '\\'; dst[j++] = 'n';     }
        else if (c >= 0x20)        { dst[j++] = (char)c;                  }
    }
    dst[j] = '\0';
}

// --- Hora (SNTP) ------------------------------------------------------------
// El handshake TLS valida la fecha del certificado del server. Al boot el reloj
// esta en 1970 -> el cert de Vercel parece "aun no valido" y la conexion falla
// con ESP_ERR_HTTP_CONNECT. Arrancamos SNTP una vez (con WiFi ya arriba) y no
// pusheamos hasta tener hora real. Es self-healing: si NTP tarda, se reintenta.
static bool ensure_time_synced(void)
{
    static bool started;
    if (!started) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&cfg) != ESP_OK) {
            ESP_LOGW(TAG, "no se pudo iniciar SNTP");
            return false;
        }
        started = true;
        ESP_LOGI(TAG, "SNTP iniciado (pool.ntp.org) para validar certificados TLS");
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) != ESP_OK) {
        ESP_LOGW(TAG, "esperando hora NTP...");
        return false;
    }
    time_t now = 0;
    time(&now);
    struct tm tm;
    gmtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2024) return false;   // aun sin hora valida
    ESP_LOGI(TAG, "hora UTC sincronizada: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

// --- Push -------------------------------------------------------------------
static esp_err_t push_snapshot(const char *reason)
{
    app_state_lock();
    const app_state_t s = *app_state_get();
    app_state_unlock();

    telemetry_snapshot_t t;
    telemetry_get_snapshot(&t);

    char prog[2 * PROG_NAME_MAX], modelo[2 * MODEL_NAME_MAX], serie[2 * SERIE_NUM_MAX];
    json_escape(prog,   sizeof prog,   s.nombre_programa);
    json_escape(modelo, sizeof modelo, s.modelo);
    json_escape(serie,  sizeof serie,  s.serie);

    char body[1400];
    int n = snprintf(body, sizeof body,
        "{"
        "\"id\":\"%s\",\"reason\":\"%s\","
        "\"temp\":%.1f,\"raw_temp\":%.1f,\"fault\":%d,"
        "\"sp_eff\":%.1f,\"sp_cfg\":%.1f,"
        "\"drv\":%.0f,\"fan\":%.0f,\"aux\":%.0f,"
        "\"hum\":%.1f,\"hum_fault\":%d,"
        "\"op_mode\":%d,\"run_state\":%d,\"warmup\":%d,\"etapa\":%u,"
        "\"elapsed_s\":%lu,\"total_s\":%lu,\"remaining_s\":%lu,"
        "\"t_min\":%.1f,\"t_max\":%.1f,"
        "\"res_wh\":%.1f,\"fan_on_s\":%.0f,\"num_mod\":%u,"
        "\"prog\":\"%s\",\"modelo\":\"%s\",\"serie\":\"%s\","
        "\"uptime_s\":%lu,"
        "\"hours_total\":%.1f,\"hours_svc\":%.1f,"
        "\"sessions\":%lu,\"sess_ok\":%lu,\"sess_int\":%lu,"
        "\"fan_faults\":%lu,\"power_fails\":%lu,\"t_max_hist\":%.1f"
        "}",
        s_dev_id, reason,
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
        s.session_energy_wh, s.session_fan_on_s, (unsigned)s.num_modulos,
        prog, modelo, serie,
        (unsigned long)s.uptime_s,
        t.hours_total_s / 3600.0f, t.hours_service_s / 3600.0f,
        (unsigned long)t.sessions_total, (unsigned long)t.sessions_completed,
        (unsigned long)t.sessions_interrupted,
        (unsigned long)t.fan_fault_count, (unsigned long)t.power_fail_count,
        t.t_max_historica);
    if (n < 0) return ESP_FAIL;
    if (n >= (int)sizeof body) n = (int)sizeof body - 1;   // truncado: improbable

    char url[160];
    snprintf(url, sizeof url, "%s%s", s_base_url, CLOUD_INGEST_PATH);
    char auth[160];
    snprintf(auth, sizeof auth, "Bearer %s", s_token);

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = CLOUD_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,   // CA bundle Mozilla (sdkconfig)
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;

    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_post_field(c, body, n);

    esp_err_t err  = esp_http_client_perform(c);
    int       code = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "push '%s' falló: %s", reason, esp_err_to_name(err));
        return err;
    }
    if (code < 200 || code >= 300) {
        ESP_LOGW(TAG, "push '%s' rechazado: HTTP %d", reason, code);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "push '%s' OK (HTTP %d, %d B)", reason, code, n);
    return ESP_OK;
}

// --- Tarea ------------------------------------------------------------------
static void cloud_task(void *arg)
{
    (void)arg;
    run_state_t     last_rs   = (run_state_t)0xFF;   // fuerza el primer envío
    TickType_t      last_push = 0;
    bool            first     = true;
    bool            time_ok   = false;
    const TickType_t interval = pdMS_TO_TICKS(CLOUD_PUSH_INTERVAL_S * 1000);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!wifi_manager_is_connected()) continue;

        // Sin hora real no se puede validar el cert TLS del server.
        if (!time_ok) {
            time_ok = ensure_time_synced();
            if (!time_ok) continue;
        }

        app_state_lock();
        run_state_t rs = app_state_get()->run_state;
        app_state_unlock();

        TickType_t now     = xTaskGetTickCount();
        bool       changed = (rs != last_rs);
        bool       due     = !first && (now - last_push) >= interval;

        if (!(first || changed || due)) continue;

        const char *reason = first ? "boot" : (changed ? "state" : "heartbeat");
        if (changed && rs == RUN_STATE_ALARM) reason = "alarm";

        if (push_snapshot(reason) == ESP_OK) {
            last_push = now;
            last_rs   = rs;
            first     = false;
        } else {
            // Backoff si el server está caído / sin internet: no martillar.
            // La alarma se reintenta igual al próximo cambio de estado.
            vTaskDelay(pdMS_TO_TICKS(CLOUD_RETRY_BACKOFF_MS));
        }
    }
}

// --- API --------------------------------------------------------------------
esp_err_t cloud_telemetry_start(void)
{
    if (s_task) return ESP_OK;

    s_provisioned = load_identity();
    if (!s_provisioned) return ESP_OK;   // inerte, no es error fatal

    // El stack (8 KB) va a PSRAM: la DRAM interna ya está casi agotada al boot
    // (~1.87 s) por LCD+LVGL+WiFi, y xTaskCreatePinnedToCore fallaba al pedir
    // 8 KB internos. Con CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y el stack
    // puede vivir en PSRAM (esta tarea nunca corre con la cache deshabilitada).
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        cloud_task, "cloud_tel", CLOUD_TASK_STACK, NULL,
        CLOUD_TASK_PRIO, &s_task, CLOUD_TASK_CORE, MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "no se pudo crear la tarea de telemetría nube");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void cloud_telemetry_stop(void)
{
    if (s_task) {
        vTaskDeleteWithCaps(s_task);   // par de xTaskCreatePinnedToCoreWithCaps
        s_task = NULL;
    }
}

bool cloud_telemetry_is_provisioned(void) { return s_provisioned; }

const char *cloud_telemetry_device_id(void) { return s_dev_id; }

#else  /* CLOUD_TELEMETRY_ENABLED == 0 — stubs no-op */

esp_err_t   cloud_telemetry_start(void)          { return ESP_OK; }
void        cloud_telemetry_stop(void)           { }
bool        cloud_telemetry_is_provisioned(void) { return false; }
const char *cloud_telemetry_device_id(void)      { return ""; }

#endif
