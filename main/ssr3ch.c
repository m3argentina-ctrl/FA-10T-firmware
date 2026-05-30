#include "ssr3ch.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"

static const char *TAG = "ssr3ch";

typedef struct {
    int      gpio;
    float    duty;
    bool     state_on;
    const char *name;
} ssr_chan_state_t;

static ssr_chan_state_t  s_chans[SSR_CH_COUNT];
static esp_timer_handle_t s_tick_timer;
static SemaphoreHandle_t  s_mtx;
static uint64_t           s_period_start_us;
static bool               s_enabled;
static bool               s_initialised;

static void chan_apply(ssr_chan_state_t *c, bool on)
{
#if !SIMULATION_MODE
    int level = on ? (SSR_ACTIVE_HIGH ? 1 : 0) : (SSR_ACTIVE_HIGH ? 0 : 1);
    gpio_set_level(c->gpio, level);
#endif
    c->state_on = on;
}

static void tick_cb(void *arg)
{
    (void)arg;
    if (xSemaphoreTake(s_mtx, 0) != pdTRUE) return;

    if (!s_enabled) {
        for (int i = 0; i < SSR_CH_COUNT; ++i) {
            if (s_chans[i].state_on) chan_apply(&s_chans[i], false);
        }
        xSemaphoreGive(s_mtx);
        return;
    }

    const uint64_t now = esp_timer_get_time();
    const uint64_t period_us = (uint64_t)SSR_PERIOD_MS * 1000ULL;
    uint64_t elapsed = now - s_period_start_us;
    if (elapsed >= period_us) {
        s_period_start_us = now;
        elapsed = 0;
    }

    for (int i = 0; i < SSR_CH_COUNT; ++i) {
        ssr_chan_state_t *c = &s_chans[i];
        const uint64_t on_us = (uint64_t)(c->duty * (float)period_us);
        const bool should_on = (c->duty >= 0.999f) ||
                               (c->duty > 0.0f && elapsed < on_us);
        if (should_on != c->state_on) chan_apply(c, should_on);
    }
    xSemaphoreGive(s_mtx);
}

esp_err_t ssr3ch_init(void)
{
    if (s_initialised) return ESP_OK;

    memset(s_chans, 0, sizeof(s_chans));
    s_chans[SSR_CH_DRV].gpio = PIN_SSR_DRV;
    s_chans[SSR_CH_DRV].name = "DRV";
    s_chans[SSR_CH_FAN].gpio = PIN_SSR_FAN;
    s_chans[SSR_CH_FAN].name = "FAN";
    s_chans[SSR_CH_AUX].gpio = PIN_SSR_AUX;
    s_chans[SSR_CH_AUX].name = "AUX";

    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

#if !SIMULATION_MODE
    for (int i = 0; i < SSR_CH_COUNT; ++i) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_chans[i].gpio,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config ch%d", i);
        gpio_set_level(s_chans[i].gpio, SSR_ACTIVE_HIGH ? 0 : 1);
    }
#endif

    const esp_timer_create_args_t args = {
        .callback        = tick_cb,
        .name            = "ssr3ch_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_tick_timer), TAG, "timer_create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, SSR_TICK_INTERVAL_MS * 1000ULL),
                        TAG, "timer_start");

    s_period_start_us = esp_timer_get_time();
    s_enabled         = true;
    s_initialised     = true;

    ESP_LOGI(TAG, "SSR3 init: DRV=GPIO%d FAN=GPIO%d AUX=GPIO%d period=%dms%s",
             PIN_SSR_DRV, PIN_SSR_FAN, PIN_SSR_AUX, SSR_PERIOD_MS,
             SIMULATION_MODE ? " [SIM]" : "");
    return ESP_OK;
}

esp_err_t ssr3ch_deinit(void)
{
    if (!s_initialised) return ESP_OK;
    esp_timer_stop(s_tick_timer);
    esp_timer_delete(s_tick_timer);
    for (int i = 0; i < SSR_CH_COUNT; ++i) chan_apply(&s_chans[i], false);
    vSemaphoreDelete(s_mtx);
    s_initialised = false;
    return ESP_OK;
}

esp_err_t ssr3ch_set_duty(ssr_channel_t ch, float duty)
{
    if (!s_initialised || ch >= SSR_CH_COUNT) return ESP_ERR_INVALID_STATE;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_chans[ch].duty = duty;
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

float ssr3ch_get_duty(ssr_channel_t ch)
{
    if (ch >= SSR_CH_COUNT) return 0.0f;
    return s_chans[ch].duty;
}

esp_err_t ssr3ch_force_all_off(void)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_enabled = false;
    for (int i = 0; i < SSR_CH_COUNT; ++i) {
        s_chans[i].duty = 0.0f;
        chan_apply(&s_chans[i], false);
    }
    xSemaphoreGive(s_mtx);
    ESP_LOGW(TAG, "all SSRs forced OFF");
    return ESP_OK;
}

esp_err_t ssr3ch_enable(void)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_enabled         = true;
    s_period_start_us = esp_timer_get_time();
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

bool ssr3ch_is_enabled(void) { return s_enabled; }
