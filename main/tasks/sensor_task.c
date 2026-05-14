#include "sensor_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"

static const char *TAG = "sensor_task";

static void sensor_task(void *arg)
{
    max31865_handle_t rtd = (max31865_handle_t)arg;
    QueueHandle_t q = app_state_sensor_queue();
    TickType_t last_wake = xTaskGetTickCount();

    safety_wdt_subscribe();

    while (1) {
        max31865_reading_t r;
        esp_err_t err = max31865_read(rtd, &r);

        sensor_sample_t sample = {
            .timestamp_us    = esp_timer_get_time(),
            .fault           = (err != ESP_OK) || r.fault,
            .fault_status    = r.fault_status,
            .raw_temperature = r.temperature,
        };

        fa10t_config_t cfg;
        app_state_copy_config(&cfg);
        sample.temperature = cfg.cal_gain * r.temperature + cfg.cal_offset;

        if (xQueueSend(q, &sample, 0) != pdTRUE) {
            // Control task is keeping up — drop oldest to avoid stale data
            sensor_sample_t drop;
            xQueueReceive(q, &drop, 0);
            xQueueSend(q, &sample, 0);
        }

        // Mirror into shared state for UI without waiting on control task
        app_state_lock();
        app_state_get()->last_sample = sample;
        app_state_unlock();

        if (sample.fault) {
            ESP_LOGW(TAG, "RTD fault status=0x%02X err=%s", r.fault_status, esp_err_to_name(err));
            max31865_clear_fault(rtd);
        }

        safety_wdt_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensor_task_start(max31865_handle_t rtd)
{
    xTaskCreatePinnedToCore(sensor_task, "sensor",
                            SENSOR_TASK_STACK, rtd,
                            SENSOR_TASK_PRIO, NULL,
                            SENSOR_TASK_CORE);
}
