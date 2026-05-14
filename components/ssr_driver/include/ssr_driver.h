#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      gpio;
    uint32_t period_ms;       // typical 1000–2000 ms for SSR with zero-cross
    bool     active_high;     // true: GPIO high = SSR on
} ssr_driver_config_t;

esp_err_t ssr_driver_init(const ssr_driver_config_t *cfg);
esp_err_t ssr_driver_deinit(void);

// Set duty cycle in [0.0, 1.0]. 0 = always off, 1 = always on.
esp_err_t ssr_driver_set_duty(float duty);

// Force OFF immediately, ignoring duty. Use for safety shutdowns.
esp_err_t ssr_driver_force_off(void);

// Resume normal operation after force_off.
esp_err_t ssr_driver_enable(void);

float ssr_driver_get_duty(void);
bool  ssr_driver_is_enabled(void);

#ifdef __cplusplus
}
#endif
