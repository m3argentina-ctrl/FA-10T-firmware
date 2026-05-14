#include "watchdog_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"

static const char *TAG = "watchdog_task";

static void reset_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BTN_RESET,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

// Press detection with hold-confirm: button is active-low. Each tick we count
// consecutive low samples; once we cross the hold threshold we fire once and
// then ignore further samples until release.
static void poll_reset_button(float current_temp)
{
    static uint32_t low_ms     = 0;
    static bool     fired      = false;
    const bool pressed = (gpio_get_level(PIN_BTN_RESET) == 0);

    if (pressed) {
        low_ms += WATCHDOG_TASK_PERIOD_MS;
        if (!fired && low_ms >= BTN_RESET_HOLD_MS) {
            ESP_LOGW(TAG, "reset button confirmed (held %lums) → safety_request_clear", (unsigned long)low_ms);
            bool ok = safety_request_clear(current_temp);
            ESP_LOGI(TAG, "  → %s", ok ? "ACCEPTED" : "REJECTED");
            fired = true;
        }
    } else {
        low_ms = 0;
        fired  = false;
    }
}

static void watchdog_task(void *arg)
{
    (void)arg;
    safety_wdt_subscribe();
    reset_button_init();

    const uint64_t t0 = esp_timer_get_time();

    while (1) {
        // Snapshot everything we need under the lock; never dereference the
        // shared state pointer outside of it.
        uint64_t last_ts;
        float    last_temp;
        uint32_t faults;
        app_state_lock();
        app_state_t *st = app_state_get();
        uint64_t now = esp_timer_get_time();
        st->uptime_s = (uint32_t)((now - t0) / 1000000ULL);
        last_ts   = st->last_sample.timestamp_us;
        last_temp = st->last_sample.temperature;
        faults    = st->safety_faults;
        app_state_unlock();

        const uint64_t s_age_us = now - last_ts;

        // If sensor stops publishing for >1 s, force SSR off through safety.
        if (last_ts != 0 && s_age_us > 1000ULL * 1000ULL) {
            ESP_LOGW(TAG, "sensor stream stale (%llu us)", (unsigned long long)s_age_us);
            safety_evaluate(0.0f, 0.0f, 0.0f, true);
        }

        if (faults != 0) {
            ESP_LOGW(TAG, "active faults: 0x%02lX", (unsigned long)faults);
        }

        poll_reset_button(last_temp);

        safety_wdt_feed();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_TASK_PERIOD_MS));
    }
}

void watchdog_task_start(void)
{
    xTaskCreatePinnedToCore(watchdog_task, "wdog",
                            WATCHDOG_TASK_STACK, NULL,
                            WATCHDOG_TASK_PRIO, NULL,
                            WATCHDOG_TASK_CORE);
}
