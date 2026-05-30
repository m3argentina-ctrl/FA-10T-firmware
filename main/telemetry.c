#include "telemetry.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "telemetry";
static const char *NS  = "fa10t_telem";
static const char *KEY = "blob";

static telemetry_snapshot_t   s_data;
static telem_event_t          s_events[TELEM_EVENT_LOG_DEPTH];
static int                    s_event_head;
static int                    s_event_count;
static SemaphoreHandle_t      s_mtx;
static float                  s_save_accum_s;
static const float            SAVE_INTERVAL_S = 300.0f;   // 5 min

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t sz = sizeof(s_data);
    nvs_get_blob(h, KEY, &s_data, &sz);
    nvs_close(h);
}

static void save_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, KEY, &s_data, sizeof(s_data));
    nvs_commit(h);
    nvs_close(h);
}

static uint32_t now_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

esp_err_t telemetry_init(void)
{
    memset(&s_data,   0, sizeof(s_data));
    memset( s_events, 0, sizeof(s_events));
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;
    load_from_nvs();
    telemetry_log_event(TELEM_EVT_BOOT, "boot");
    ESP_LOGI(TAG, "telemetry init: hrs=%lu, hrs_svc=%lu, sessions=%lu",
             (unsigned long)(s_data.hours_total_s / 3600),
             (unsigned long)(s_data.hours_service_s / 3600),
             (unsigned long)s_data.sessions_total);
    return ESP_OK;
}

void telemetry_log_event(telem_event_kind_t kind, const char *fmt, ...)
{
    if (!s_mtx) return;
    char buf[TELEM_EVENT_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    telem_event_t *e = &s_events[s_event_head];
    e->timestamp_uptime_s = now_uptime_s();
    e->kind               = kind;
    snprintf(e->msg, TELEM_EVENT_MSG_MAX, "%s", buf);
    s_event_head = (s_event_head + 1) % TELEM_EVENT_LOG_DEPTH;
    if (s_event_count < TELEM_EVENT_LOG_DEPTH) s_event_count++;
    xSemaphoreGive(s_mtx);
}

void telemetry_tick(float dt_s, bool session_running)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_data.hours_service_s += (uint32_t)(dt_s + 0.0001f);
    if (session_running) s_data.hours_total_s += (uint32_t)(dt_s + 0.0001f);
    s_save_accum_s += dt_s;
    bool do_save = (s_save_accum_s >= SAVE_INTERVAL_S);
    if (do_save) s_save_accum_s = 0.0f;
    xSemaphoreGive(s_mtx);
    if (do_save) save_to_nvs();
}

void telemetry_note_session_start(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_data.sessions_total++;
    xSemaphoreGive(s_mtx);
    telemetry_log_event(TELEM_EVT_SESSION_START, "session #%lu",
                        (unsigned long)s_data.sessions_total);
}

void telemetry_note_session_end(bool completed)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (completed) s_data.sessions_completed++;
    else           s_data.sessions_interrupted++;
    xSemaphoreGive(s_mtx);
    telemetry_log_event(TELEM_EVT_SESSION_END, completed ? "completed" : "interrupted");
    save_to_nvs();
}

void telemetry_note_fan_fault(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_data.fan_fault_count++;
    xSemaphoreGive(s_mtx);
    telemetry_log_event(TELEM_EVT_FAULT, "fan fault");
}

void telemetry_note_ssr_edge(int channel)
{
    if (channel < 0 || channel > 2) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (channel == 0) s_data.ssr_cycles_drv++;
    if (channel == 1) s_data.ssr_cycles_fan++;
    xSemaphoreGive(s_mtx);
}

void telemetry_note_temperature(float t)
{
    if (t < 0.0f || t > 200.0f) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (t > s_data.t_max_historica) s_data.t_max_historica = t;
    xSemaphoreGive(s_mtx);
}

esp_err_t telemetry_reset_service(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_data.hours_service_s = 0;
    xSemaphoreGive(s_mtx);
    telemetry_log_event(TELEM_EVT_SERVICE, "service ack");
    save_to_nvs();
    return ESP_OK;
}

void telemetry_get_snapshot(telemetry_snapshot_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mtx);
}

void telemetry_get_events(telem_event_t *out, int *n)
{
    if (!out || !n) return;
    int cap = *n;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int copy = (s_event_count < cap) ? s_event_count : cap;
    // Most-recent-first: walk back from head.
    for (int i = 0; i < copy; ++i) {
        int idx = (s_event_head - 1 - i + TELEM_EVENT_LOG_DEPTH) % TELEM_EVENT_LOG_DEPTH;
        out[i] = s_events[idx];
    }
    *n = copy;
    xSemaphoreGive(s_mtx);
}
