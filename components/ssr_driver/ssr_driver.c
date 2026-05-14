#include "ssr_driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ssr_driver";

typedef struct {
    ssr_driver_config_t cfg;
    esp_timer_handle_t  tick_timer;
    SemaphoreHandle_t   mtx;
    float               duty;        // 0..1
    uint64_t            period_start_us;
    bool                state_on;
    bool                enabled;
    bool                initialised;
} ssr_state_t;

static ssr_state_t s_ssr;

static void set_gpio(bool on)
{
    int level = on ^ !s_ssr.cfg.active_high ? 1 : 0;
    gpio_set_level(s_ssr.cfg.gpio, level);
    s_ssr.state_on = on;
}

static void tick_cb(void *arg)
{
    if (xSemaphoreTake(s_ssr.mtx, 0) != pdTRUE) return;

    if (!s_ssr.enabled) {
        set_gpio(false);
        xSemaphoreGive(s_ssr.mtx);
        return;
    }

    const uint64_t now = esp_timer_get_time();
    const uint64_t period_us = (uint64_t)s_ssr.cfg.period_ms * 1000ULL;
    uint64_t elapsed = now - s_ssr.period_start_us;
    if (elapsed >= period_us) {
        s_ssr.period_start_us = now;
        elapsed = 0;
    }

    const uint64_t on_us = (uint64_t)(s_ssr.duty * (float)period_us);
    const bool should_on = (s_ssr.duty >= 0.999f) ||
                           (s_ssr.duty > 0.0f && elapsed < on_us);
    if (should_on != s_ssr.state_on) {
        set_gpio(should_on);
    }
    xSemaphoreGive(s_ssr.mtx);
}

esp_err_t ssr_driver_init(const ssr_driver_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->period_ms >= 100, ESP_ERR_INVALID_ARG, TAG, "bad cfg");
    if (s_ssr.initialised) return ESP_ERR_INVALID_STATE;

    memset(&s_ssr, 0, sizeof(s_ssr));
    s_ssr.cfg = *cfg;
    s_ssr.mtx = xSemaphoreCreateMutex();
    if (!s_ssr.mtx) return ESP_ERR_NO_MEM;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->gpio,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config");
    gpio_set_level(cfg->gpio, cfg->active_high ? 0 : 1);

    const esp_timer_create_args_t args = {
        .callback        = tick_cb,
        .name            = "ssr_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_ssr.tick_timer), TAG, "timer_create");

    // 50 ms tick is more than enough for ~1 s SSR period
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_ssr.tick_timer, 50 * 1000), TAG, "timer_start");

    s_ssr.period_start_us = esp_timer_get_time();
    s_ssr.enabled = true;
    s_ssr.initialised = true;
    ESP_LOGI(TAG, "SSR on GPIO %d, period %lu ms", cfg->gpio, (unsigned long)cfg->period_ms);
    return ESP_OK;
}

esp_err_t ssr_driver_deinit(void)
{
    if (!s_ssr.initialised) return ESP_OK;
    esp_timer_stop(s_ssr.tick_timer);
    esp_timer_delete(s_ssr.tick_timer);
    set_gpio(false);
    vSemaphoreDelete(s_ssr.mtx);
    s_ssr.initialised = false;
    return ESP_OK;
}

esp_err_t ssr_driver_set_duty(float duty)
{
    if (!s_ssr.initialised) return ESP_ERR_INVALID_STATE;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    xSemaphoreTake(s_ssr.mtx, portMAX_DELAY);
    s_ssr.duty = duty;
    xSemaphoreGive(s_ssr.mtx);
    return ESP_OK;
}

esp_err_t ssr_driver_force_off(void)
{
    if (!s_ssr.initialised) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ssr.mtx, portMAX_DELAY);
    s_ssr.enabled = false;
    s_ssr.duty    = 0.0f;
    set_gpio(false);
    xSemaphoreGive(s_ssr.mtx);
    ESP_LOGW(TAG, "SSR forced OFF");
    return ESP_OK;
}

esp_err_t ssr_driver_enable(void)
{
    if (!s_ssr.initialised) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ssr.mtx, portMAX_DELAY);
    s_ssr.enabled = true;
    s_ssr.period_start_us = esp_timer_get_time();
    xSemaphoreGive(s_ssr.mtx);
    return ESP_OK;
}

float ssr_driver_get_duty(void)        { return s_ssr.duty;    }
bool  ssr_driver_is_enabled(void)      { return s_ssr.enabled; }
