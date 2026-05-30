#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSR_CH_DRV = 0,    // heaters
    SSR_CH_FAN = 1,    // turbines
    SSR_CH_AUX = 2,    // 4" extractor
    SSR_CH_COUNT
} ssr_channel_t;

esp_err_t ssr3ch_init(void);
esp_err_t ssr3ch_deinit(void);

// Set time-proportional duty for a single channel, [0.0, 1.0].
esp_err_t ssr3ch_set_duty(ssr_channel_t ch, float duty);
float     ssr3ch_get_duty(ssr_channel_t ch);

// Safety: drive every channel OFF immediately and disable the controller.
// ssr3ch_enable() resumes normal duty-cycle operation.
esp_err_t ssr3ch_force_all_off(void);
esp_err_t ssr3ch_enable(void);
bool      ssr3ch_is_enabled(void);

#ifdef __cplusplus
}
#endif
