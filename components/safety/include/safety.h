#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAFETY_OK            = 0,
    SAFETY_OVERTEMP      = 1 << 0,
    SAFETY_SENSOR_FAULT  = 1 << 1,
    SAFETY_RUNAWAY       = 1 << 2,
    SAFETY_WDT_TIMEOUT   = 1 << 3,
} safety_fault_t;

typedef struct {
    float    temp_max_c;        // hard upper limit
    float    runaway_dt_c;      // min ΔT expected in window when calling for heat
    float    runaway_window_s;  // observation window
    float    runaway_duty_thr;  // only evaluate runaway when duty > this (e.g. 0.4)
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
void     safety_clear_latched(void);

#ifdef __cplusplus
}
#endif
