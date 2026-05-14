#include "control_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "pid_controller.h"
#include "ssr_driver.h"
#include "safety.h"

static const char *TAG = "control_task";

static void apply_config_to_pid(pid_t *pid, const fa10t_config_t *cfg)
{
    pid_params_t p = {
        .kp = cfg->kp,
        .ki = cfg->ki,
        .kd = cfg->kd,
        .out_min = 0.0f,
        .out_max = 1.0f,
        .d_filter_alpha = 0.85f,
        .derivative_on_measurement = true,
    };
    pid_set_params(pid, &p);
    pid_set_setpoint(pid, cfg->setpoint);
}

static void control_task(void *arg)
{
    (void)arg;
    pid_t pid;
    pid_params_t init = {
        .kp = 8.0f, .ki = 0.15f, .kd = 40.0f,
        .out_min = 0.0f, .out_max = 1.0f,
        .d_filter_alpha = 0.85f,
        .derivative_on_measurement = true,
    };
    pid_init(&pid, &init);

    fa10t_config_t cfg;
    app_state_copy_config(&cfg);
    apply_config_to_pid(&pid, &cfg);

    QueueHandle_t q = app_state_sensor_queue();
    uint64_t last_us = esp_timer_get_time();

    safety_wdt_subscribe();

    while (1) {
        sensor_sample_t sample;
        if (xQueueReceive(q, &sample, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS + 50)) != pdTRUE) {
            // No fresh sample: treat as sensor fault for safety eval
            sample.fault       = true;
            sample.temperature = 0.0f;
            sample.timestamp_us = esp_timer_get_time();
        }

        uint64_t now_us = esp_timer_get_time();
        float dt = (now_us - last_us) / 1.0e6f;
        if (dt <= 0.0f || dt > 1.0f) dt = (float)CONTROL_TASK_PERIOD_MS / 1000.0f;
        last_us = now_us;

        // Refresh PID setpoint/gains from a thread-safe snapshot of the config.
        app_state_copy_config(&cfg);
        apply_config_to_pid(&pid, &cfg);

        // PID first, then fault override (avoids divergent integral on faults).
        float out;
        if (sample.fault) {
            pid_reset(&pid);
            out = 0.0f;
        } else {
            out = pid_compute(&pid, sample.temperature, dt);
        }

        uint32_t faults = safety_evaluate(sample.temperature, out, dt, sample.fault);

        // After a recovery (manual ack or transient sensor fault clearing),
        // reset PID state once so we don't dump accumulated integral into a
        // freshly re-armed heater.
        if (safety_consume_recovery_event()) {
            pid_reset(&pid);
            ESP_LOGI(TAG, "PID reset after safety recovery");
        }

        float duty_out;
        if (faults != 0) {
            duty_out = 0.0f;
        } else {
            // Apply soft-start ramp during recovery window.
            duty_out = out * safety_recovery_factor();
            ssr_driver_set_duty(duty_out);
        }

        // Publish to shared state
        app_state_lock();
        app_state_t *st = app_state_get();
        st->pid_output    = out;
        st->ssr_duty      = ssr_driver_get_duty();
        st->safety_faults = faults;
        st->setpoint      = cfg.setpoint;
        st->running       = (faults == 0);
        app_state_unlock();

        safety_wdt_feed();
    }
}

void control_task_start(void)
{
    xTaskCreatePinnedToCore(control_task, "control",
                            CONTROL_TASK_STACK, NULL,
                            CONTROL_TASK_PRIO, NULL,
                            CONTROL_TASK_CORE);
}
