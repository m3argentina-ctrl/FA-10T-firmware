#include "watchdog_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"

static const char *TAG = "watchdog_task";

static void watchdog_task(void *arg)
{
    (void)arg;
    safety_wdt_subscribe();
    const uint64_t t0 = esp_timer_get_time();
    uint64_t last_sample_us = t0;

    while (1) {
        app_state_lock();
        app_state_t *st = app_state_get();
        uint64_t now = esp_timer_get_time();
        st->uptime_s = (uint32_t)((now - t0) / 1000000ULL);
        uint64_t s_age_us = now - st->last_sample.timestamp_us;
        if (st->last_sample.timestamp_us > last_sample_us) {
            last_sample_us = st->last_sample.timestamp_us;
        }
        uint32_t faults = st->safety_faults;
        app_state_unlock();

        // If sensor stops publishing for >1 s, force SSR off
        if (s_age_us > 1000 * 1000 && st->last_sample.timestamp_us != 0) {
            ESP_LOGW(TAG, "sensor stream stale (%llu us)", s_age_us);
            safety_evaluate(0.0f, 0.0f, 0.0f, true);
        }

        if (faults != 0) {
            ESP_LOGW(TAG, "active faults: 0x%02lX", (unsigned long)faults);
        }

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
