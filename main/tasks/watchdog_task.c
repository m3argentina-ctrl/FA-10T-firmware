#include "watchdog_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"
#include "telemetry.h"
#include "recovery.h"
#include "tasks/sensor_task.h"
#include "tasks/control_task.h"
#include "tasks/ui_task.h"

static const char *TAG = "watchdog_task";

static TaskHandle_t s_handle;
TaskHandle_t watchdog_task_handle(void) { return s_handle; }

// Log stack high-water marks for every monitored task. Called every N ticks
// from the watchdog loop so the operator can spot stack pressure before a
// stack overflow panics the system. Reported value is the smallest free stack
// (in WORDS, not bytes) ever observed.
static void log_stack_usage(void)
{
    const struct {
        const char  *name;
        TaskHandle_t (*get)(void);
    } tasks[] = {
        { "sensor",  sensor_task_handle  },
        { "control", control_task_handle },
        { "ui",      ui_task_handle      },
        { "wdog",    watchdog_task_handle },
    };
    for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        TaskHandle_t h = tasks[i].get();
        if (!h) continue;
        UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(h);
        ESP_LOGI(TAG, "stack hwm %-7s = %u words (%u bytes)",
                 tasks[i].name,
                 (unsigned)hwm_words,
                 (unsigned)(hwm_words * sizeof(StackType_t)));
    }
}

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
    // Log stack usage every ~30s. Period is WATCHDOG_TASK_PERIOD_MS (=500),
    // so 60 ticks ≈ 30s.
    const uint32_t stack_log_every = (30 * 1000) / WATCHDOG_TASK_PERIOD_MS;
    uint32_t stack_log_counter = 0;

    const float wdog_dt_s = (float)WATCHDOG_TASK_PERIOD_MS / 1000.0f;

    while (1) {
        // Snapshot everything we need under the lock; never dereference the
        // shared state pointer outside of it.
        uint64_t last_ts;
        float    last_temp;
        uint32_t faults;
        bool     session_running;
        app_state_lock();
        app_state_t *st = app_state_get();
        uint64_t now = esp_timer_get_time();
        st->uptime_s = (uint32_t)((now - t0) / 1000000ULL);
        last_ts         = st->last_sample.timestamp_us;
        last_temp       = st->last_sample.temperature;
        faults          = st->safety_faults;
        session_running = (st->run_state == RUN_STATE_RUNNING);
        app_state_unlock();

        telemetry_tick(wdog_dt_s, session_running);
        recovery_tick(wdog_dt_s);

        const uint64_t s_age_us = now - last_ts;

        // If sensor stops publishing for >1 s, force SSR off through safety.
        if (last_ts != 0 && s_age_us > 1000ULL * 1000ULL) {
            ESP_LOGW(TAG, "sensor stream stale (%llu us)", (unsigned long long)s_age_us);
            // sensor stale → sensor_fault=true; límite en 0 (no aplica sin lectura).
            safety_evaluate(0.0f, 0.0f, 0.0f, 0.0f, true, false);
        }

        if (faults & SAFETY_TRIP_MASK) {
            ESP_LOGW(TAG, "active TRIP faults: 0x%04lX", (unsigned long)(faults & SAFETY_TRIP_MASK));
        }
        if (faults & SAFETY_WARN_MASK) {
            ESP_LOGW(TAG, "warnings: 0x%04lX", (unsigned long)(faults & SAFETY_WARN_MASK));
        }

        poll_reset_button(last_temp);

        if (++stack_log_counter >= stack_log_every) {
            stack_log_counter = 0;
            log_stack_usage();
        }

        safety_wdt_feed();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_TASK_PERIOD_MS));
    }
}

void watchdog_task_start(void)
{
    xTaskCreatePinnedToCore(watchdog_task, "wdog",
                            WATCHDOG_TASK_STACK, NULL,
                            WATCHDOG_TASK_PRIO, &s_handle,
                            WATCHDOG_TASK_CORE);
}
