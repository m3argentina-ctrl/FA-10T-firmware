#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAFETY_OK            = 0,
    // Trip bits — SSR is forced OFF when any of these is set.
    SAFETY_OVERTEMP      = 1 << 0,
    SAFETY_SENSOR_FAULT  = 1 << 1,
    SAFETY_RUNAWAY       = 1 << 2,
    SAFETY_WDT_TIMEOUT   = 1 << 3,
    // Warning bits — diagnostic only, do NOT cut the SSR. The UI/logs can use
    // these to flag conditions that warrant attention but are not yet faults.
    SAFETY_WARN_NEAR_LIMIT = 1 << 8,   // T within hysteresis of temp_max_c
} safety_fault_t;

#define SAFETY_TRIP_MASK   (SAFETY_OVERTEMP | SAFETY_SENSOR_FAULT | SAFETY_RUNAWAY | SAFETY_WDT_TIMEOUT)
#define SAFETY_WARN_MASK   (SAFETY_WARN_NEAR_LIMIT)

typedef struct {
    float    temp_max_c;        // hard upper limit
    float    runaway_dt_c;      // min ΔT expected in window when calling for heat
    float    runaway_window_s;  // observation window
    float    runaway_duty_thr;  // only evaluate runaway when duty > this (e.g. 0.4)
    float    hysteresis_c;      // T must drop this much below limit before a clear is accepted
    float    recovery_ramp_s;   // soft-start ramp duration after a trip (seconds)
    uint32_t wdt_timeout_s;     // task watchdog timeout
} safety_config_t;

esp_err_t safety_init(const safety_config_t *cfg);

// Subscribe the calling task to the Task Watchdog Timer.
esp_err_t safety_wdt_subscribe(void);
esp_err_t safety_wdt_feed(void);
esp_err_t safety_wdt_unsubscribe(void);

// Feed the runaway / overtemp detector. Call each control cycle.
//   temperature: current measurement, °C
//   duty:        current SSR duty 0..1
//   dt_s:        seconds since last call
//   sensor_fault: true if the sensor reading is invalid
// Returns the bitmask of active faults. Any non-zero result already forced
// the SSR off via ssr_driver_force_off().
uint32_t safety_evaluate(float temperature, float duty, float dt_s, bool sensor_fault);

uint32_t safety_get_faults(void);

// Operator-confirmed clear of latched faults. Returns true if the clear was
// accepted. Rejected when temperature is still within hysteresis of the
// configured limit (caller passes the latest measurement).
bool     safety_request_clear(float current_temp);

// Returns true exactly once after each recovery (latch cleared and SSR
// re-enabled). The control loop polls this to reset PID state.
bool     safety_consume_recovery_event(void);

// Soft-start factor in [0..1] applied by the control loop after a recovery.
// Returns 1.0 outside of a recovery ramp.
float    safety_recovery_factor(void);

#ifdef __cplusplus
}
#endif
