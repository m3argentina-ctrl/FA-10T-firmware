#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAFETY_OK              = 0,
    // Trip bits — all SSRs are forced OFF when any of these is set.
    SAFETY_OVERTEMP        = 1 << 0,
    SAFETY_SENSOR_FAULT    = 1 << 1,
    SAFETY_RUNAWAY         = 1 << 2,
    SAFETY_WDT_TIMEOUT     = 1 << 3,
    SAFETY_FAN_FAULT       = 1 << 4,
    // Warning bits — diagnostics only, do NOT cut the SSR.
    SAFETY_WARN_NEAR_LIMIT = 1 << 8,
} safety_fault_t;

#define SAFETY_TRIP_MASK   (SAFETY_OVERTEMP | SAFETY_SENSOR_FAULT | SAFETY_RUNAWAY | \
                            SAFETY_WDT_TIMEOUT | SAFETY_FAN_FAULT)
#define SAFETY_WARN_MASK   (SAFETY_WARN_NEAR_LIMIT)

typedef struct {
    float    temp_max_c;
    float    runaway_dt_c;
    float    runaway_window_s;
    float    runaway_duty_thr;
    float    hysteresis_c;
    float    recovery_ramp_s;
    uint32_t wdt_timeout_s;
} safety_config_t;

esp_err_t safety_init(const safety_config_t *cfg);

esp_err_t safety_wdt_subscribe(void);
esp_err_t safety_wdt_feed(void);
esp_err_t safety_wdt_unsubscribe(void);

// Feed the runaway / overtemp / fan-fault detector. Call each control cycle.
// fan_fault: caller-supplied, true if turbine current < 70% nominal.
uint32_t safety_evaluate(float temperature, float duty, float dt_s,
                         bool sensor_fault, bool fan_fault);

uint32_t safety_get_faults(void);

bool     safety_request_clear(float current_temp);

bool     safety_consume_recovery_event(void);

float    safety_recovery_factor(void);

#ifdef __cplusplus
}
#endif
