#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_CONFIG_NAMESPACE  "fa10t"
#define NVS_CONFIG_VERSION    1

typedef struct {
    uint16_t version;

    // PID gains
    float kp;
    float ki;
    float kd;

    // Setpoint (°C)
    float setpoint;

    // Safety limits
    float temp_max_c;       // hard upper limit, trip SSR off above this
    float runaway_dt_c;     // expected min ΔT in window when heating
    float runaway_window_s; // window length

    // Sensor calibration (linear: T_real = a*T_meas + b)
    float cal_gain;
    float cal_offset;

    // SSR
    uint16_t ssr_period_ms;
} fa10t_config_t;

esp_err_t nvs_config_init(void);

// Load saved config; returns defaults if missing or version mismatch.
esp_err_t nvs_config_load(fa10t_config_t *out);

esp_err_t nvs_config_save(const fa10t_config_t *cfg);
esp_err_t nvs_config_erase(void);

// In-RAM defaults.
void      nvs_config_defaults(fa10t_config_t *out);

#ifdef __cplusplus
}
#endif
