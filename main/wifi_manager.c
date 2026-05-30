#include "wifi_manager.h"
#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"

#if WIFI_ENABLED

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_mgr";

#define WIFI_NS              "fa10t_wifi"
#define MAX_STA_RETRIES      5
// Equipo remoto siempre-encendido: tras agotar el burst de reintentos rápidos
// NO se abandona la conexión. Se pasa a un reintento lento e indefinido cada
// STA_BACKOFF_MS, así un corte de WiFi/router se recupera solo sin reboot.
#define STA_BACKOFF_MS       30000
#define STA_BIT_CONNECTED    (1 << 0)
#define STA_BIT_FAILED       (1 << 1)
#define WIFI_SCAN_MAX        20

static EventGroupHandle_t   s_evt_group;
static wifi_mgr_state_t     s_state;
static int                  s_retries;
static char                 s_ssid[33];
static char                 s_pass[65];
static esp_ip4_addr_t       s_ip;
static bool                 s_initialised;
static esp_timer_handle_t   s_reconnect_timer;   // backoff de reconexión indefinida

// Resultados del último escaneo. Los llena el event handler (tarea WiFi) al
// recibir WIFI_EVENT_SCAN_DONE; los lee la UI. s_scan_ready hace de barrera.
static wifi_scan_ap_t       s_scan[WIFI_SCAN_MAX];
static int                  s_scan_n;
static volatile bool        s_scan_ready;

// --- NVS ----------------------------------------------------------------------
static void load_creds_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t sz = sizeof(s_ssid);
    nvs_get_str(h, "ssid", s_ssid, &sz);
    sz = sizeof(s_pass);
    nvs_get_str(h, "pass", s_pass, &sz);
    nvs_close(h);
    if (s_ssid[0]) ESP_LOGI(TAG, "loaded SSID='%s' from NVS", s_ssid);
}

static esp_err_t save_creds_to_nvs(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", ssid ? ssid : "");
    if (err == ESP_OK) err = nvs_set_str(h, "pass", pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// Reintento lento de reconexión (corre en la tarea de esp_timer). Se re-arma
// solo: si esp_wifi_connect falla, llega otro STA_DISCONNECTED que vuelve a
// programar el timer. Nunca se abandona.
static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "reintentando conexión a '%s' (backoff)", s_ssid);
    esp_wifi_connect();
}

// --- Event handler ------------------------------------------------------------
static void evt_handler(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_ssid[0]) {
                ESP_LOGI(TAG, "STA start — connecting to '%s'", s_ssid);
                s_state = WIFI_MGR_STATE_CONNECTING;
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retries < MAX_STA_RETRIES) {
                // Burst inicial: reintentos rápidos back-to-back.
                s_retries++;
                s_state = WIFI_MGR_STATE_CONNECTING;
                ESP_LOGW(TAG, "STA disconnected, retry %d/%d",
                         s_retries, MAX_STA_RETRIES);
                esp_wifi_connect();
            } else {
                // Agotado el burst: NO se abandona. El panel muestra "FAILED"
                // (rojo) pero seguimos reintentando lento cada STA_BACKOFF_MS
                // para siempre — un equipo remoto debe recuperarse solo.
                if (s_retries == MAX_STA_RETRIES) {
                    s_retries++;   // log de transición una sola vez
                    ESP_LOGW(TAG, "STA sin conectar tras %d intentos; "
                             "reintento lento cada %d s (indefinido)",
                             MAX_STA_RETRIES, STA_BACKOFF_MS / 1000);
                    s_state = WIFI_MGR_STATE_FAILED;
                    xEventGroupSetBits(s_evt_group, STA_BIT_FAILED);
                }
                esp_timer_stop(s_reconnect_timer);   // idempotente
                esp_timer_start_once(s_reconnect_timer,
                                     (uint64_t)STA_BACKOFF_MS * 1000);
            }
            break;
        case WIFI_EVENT_SCAN_DONE: {
            // El escaneo terminó: copiar los registros internos a s_scan[].
            // recs es static para no estresar el stack de la tarea de eventos.
            static wifi_ap_record_t recs[WIFI_SCAN_MAX];
            uint16_t num = WIFI_SCAN_MAX;
            if (esp_wifi_scan_get_ap_records(&num, recs) == ESP_OK) {
                int n = 0;
                for (int i = 0; i < num && n < WIFI_SCAN_MAX; ++i) {
                    if (recs[i].ssid[0] == '\0') continue;   // saltar redes ocultas
                    strlcpy(s_scan[n].ssid, (char *)recs[i].ssid, sizeof(s_scan[n].ssid));
                    s_scan[n].rssi   = recs[i].rssi;
                    s_scan[n].secure = (recs[i].authmode != WIFI_AUTH_OPEN);
                    n++;
                }
                s_scan_n = n;
            } else {
                s_scan_n = 0;
            }
            s_scan_ready = true;
            ESP_LOGI(TAG, "scan done: %d redes", s_scan_n);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_ip = e->ip_info.ip;
        s_retries = 0;
        esp_timer_stop(s_reconnect_timer);   // cancelar backoff pendiente
        s_state = WIFI_MGR_STATE_CONNECTED;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&s_ip));
        xEventGroupSetBits(s_evt_group, STA_BIT_CONNECTED);
    }
}

// --- AP/STA config helpers ----------------------------------------------------
static void apply_sta_config(void)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid,     s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable    = true;
    cfg.sta.pmf_cfg.required   = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
}

// --- API pública --------------------------------------------------------------
esp_err_t wifi_manager_init(void)
{
    if (s_initialised) return ESP_OK;

    s_evt_group = xEventGroupCreate();
    if (!s_evt_group) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t rc_args = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&rc_args, &s_reconnect_timer),
                        TAG, "reconnect_timer");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event_loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi_init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, evt_handler, NULL, NULL), TAG, "evt wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, evt_handler, NULL, NULL), TAG, "evt ip");

    // STA-only (decisión Emilio 2026-05-30): el equipo se conecta a la red del
    // cliente y no levanta AP propio. Más limpio/seguro en planta.
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");

    // Cargar STA creds desde NVS si existen → autoconecta al boot.
    load_creds_from_nvs();
    if (s_ssid[0]) apply_sta_config();

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi_start");

    s_state       = s_ssid[0] ? WIFI_MGR_STATE_CONNECTING : WIFI_MGR_STATE_AP_ONLY;
    s_initialised = true;
    ESP_LOGI(TAG, "WiFi STA up: %s",
             s_ssid[0] ? s_ssid : "(sin red configurada)");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;

    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, password ? password : "", sizeof(s_pass));
    save_creds_to_nvs(s_ssid, s_pass);

    s_retries = 0;
    s_state   = WIFI_MGR_STATE_CONNECTING;
    esp_timer_stop(s_reconnect_timer);   // cancelar backoff de una red previa
    esp_wifi_disconnect();
    apply_sta_config();
    return esp_wifi_connect();
}

esp_err_t wifi_manager_start_ap(const char *ssid)
{
    (void)ssid;
    // Build STA-only: no se levanta AP. Se mantiene la función por contrato de API.
    ESP_LOGW(TAG, "start_ap ignorado: firmware compilado STA-only");
    return ESP_ERR_NOT_SUPPORTED;
}

bool wifi_manager_is_connected(void) { return s_state == WIFI_MGR_STATE_CONNECTED; }
wifi_mgr_state_t wifi_manager_state(void) { return s_state; }
const char *wifi_manager_get_ssid(void)  { return s_ssid[0] ? s_ssid : NULL; }

bool wifi_manager_get_ip(char *buf, size_t sz)
{
    if (s_state != WIFI_MGR_STATE_CONNECTED || !buf || sz < 16) return false;
    snprintf(buf, sz, IPSTR, IP2STR(&s_ip));
    return true;
}

// --- Escaneo ------------------------------------------------------------------
esp_err_t wifi_manager_scan_start(void)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    s_scan_ready = false;
    wifi_scan_config_t cfg = { .show_hidden = false };
    return esp_wifi_scan_start(&cfg, false);   // false = no bloqueante
}

bool wifi_manager_scan_ready(void) { return s_scan_ready; }

int wifi_manager_scan_results(wifi_scan_ap_t *out, int max)
{
    if (!out || max <= 0) return 0;
    int n = (s_scan_n < max) ? s_scan_n : max;
    memcpy(out, s_scan, (size_t)n * sizeof(wifi_scan_ap_t));
    s_scan_ready = false;
    return n;
}

#else  /* WIFI_ENABLED == 0 — stubs no-op para mantener API enlazable */

esp_err_t wifi_manager_init(void)                                       { return ESP_OK; }
esp_err_t wifi_manager_connect(const char *s, const char *p)            { (void)s; (void)p; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wifi_manager_start_ap(const char *s)                          { (void)s; return ESP_ERR_NOT_SUPPORTED; }
bool      wifi_manager_is_connected(void)                               { return false; }
wifi_mgr_state_t wifi_manager_state(void)                               { return WIFI_MGR_STATE_OFF; }
const char *wifi_manager_get_ssid(void)                                 { return NULL; }
bool      wifi_manager_get_ip(char *buf, size_t sz)                     { (void)buf; (void)sz; return false; }
esp_err_t wifi_manager_scan_start(void)                                 { return ESP_ERR_NOT_SUPPORTED; }
bool      wifi_manager_scan_ready(void)                                 { return false; }
int       wifi_manager_scan_results(wifi_scan_ap_t *o, int m)           { (void)o; (void)m; return 0; }

#endif /* WIFI_ENABLED */
