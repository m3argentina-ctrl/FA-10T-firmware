#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELEM_EVENT_LOG_DEPTH   16
#define TELEM_EVENT_MSG_MAX     56

typedef enum {
    TELEM_EVT_BOOT          = 0,
    TELEM_EVT_SESSION_START = 1,
    TELEM_EVT_SESSION_END   = 2,
    TELEM_EVT_POWER_FAIL    = 3,
    TELEM_EVT_FAULT         = 4,
    TELEM_EVT_SERVICE       = 5,
} telem_event_kind_t;

typedef struct {
    uint32_t           timestamp_uptime_s;
    telem_event_kind_t kind;
    char               msg[TELEM_EVENT_MSG_MAX];
} telem_event_t;

typedef struct {
    uint32_t hours_total_s;       // seconds; convert to hours on read
    uint32_t hours_service_s;     // since last service ack
    uint32_t ssr_cycles_drv;
    uint32_t ssr_cycles_fan;
    uint32_t sessions_total;
    uint32_t sessions_completed;
    uint32_t sessions_interrupted;
    uint32_t power_fail_count;
    uint32_t fan_fault_count;
    float    t_max_historica;
} telemetry_snapshot_t;

esp_err_t telemetry_init(void);

// Called periodically from a low-priority task — accumulates uptime and
// flushes to NVS at a sensible cadence.
void      telemetry_tick(float dt_s, bool session_running);

// Hooks invoked from various modules.
void      telemetry_log_event(telem_event_kind_t kind, const char *fmt, ...);
void      telemetry_note_session_start(void);
void      telemetry_note_session_end(bool completed);
void      telemetry_note_fan_fault(void);
void      telemetry_note_ssr_edge(int channel);
void      telemetry_note_temperature(float t);

esp_err_t telemetry_reset_service(void);   // operator ack after service

void      telemetry_get_snapshot(telemetry_snapshot_t *out);
void      telemetry_get_events(telem_event_t *out, int *n);

#ifdef __cplusplus
}
#endif
