#include "sensor_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"
#include "pt1000_adc.h"
#include "acs712.h"
#include "sht31.h"

static const char *TAG = "sensor_task";

static TaskHandle_t s_handle;
TaskHandle_t sensor_task_handle(void) { return s_handle; }

// Sub-rate counters: PT1000 every cycle, ACS712 every 5 (2 Hz), SHT31 every 10 (1 Hz).
#define ACS_DIVIDER  (500  / SENSOR_TASK_PERIOD_MS)   // 5
#define SHT_DIVIDER  (1000 / SENSOR_TASK_PERIOD_MS)   // 10

static void sensor_task(void *arg)
{
    (void)arg;
    QueueHandle_t q = app_state_sensor_queue();
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0;

    safety_wdt_subscribe();

    while (1) {
        // --- PT1000 @ 10 Hz ---
        pt1000_reading_t r = {0};
        esp_err_t err = pt1000_adc_read(&r);

        sensor_sample_t sample = {
            .timestamp_us    = esp_timer_get_time(),
            .fault           = (err != ESP_OK) || r.fault,
            .fault_status    = r.fault_status,
            .raw_temperature = r.temperature_c,
        };

        fa10t_config_t cfg;
        app_state_copy_config(&cfg);
        sample.temperature = cfg.cal_gain * r.temperature_c + cfg.cal_offset;

        xQueueOverwrite(q, &sample);

        app_state_lock();
        app_state_get()->last_sample = sample;
        app_state_unlock();

        if (sample.fault) {
            ESP_LOGW(TAG, "PT1000 fault status=0x%02X err=%s V=%.3f T=%.1f",
                     r.fault_status, esp_err_to_name(err), r.voltage_v, r.temperature_c);
        }

        // --- ACS712 @ 2 Hz ---
        if ((tick % ACS_DIVIDER) == 0) {
            float amps = 0.0f;
            if (acs712_read_rms(&amps) == ESP_OK) {
                app_state_lock();
                app_state_t *st = app_state_get();
                st->fan_current = amps;
                if (st->fan_nominal_known && st->fan_nominal > 0.05f) {
                    st->fan_fault = (amps < ACS712_FAULT_RATIO * st->fan_nominal);
                } else {
                    st->fan_fault = false;
                }
                app_state_unlock();
            }
        }

        // --- SHT31 @ 1 Hz ---
        if ((tick % SHT_DIVIDER) == 0) {
            float rh = 0.0f, t_amb = 0.0f;
            esp_err_t serr = sht31_read(&rh, &t_amb);
            app_state_lock();
            app_state_t *st = app_state_get();
            if (serr == ESP_OK) {
                st->humidity       = rh;
                st->sht31_temp     = t_amb;
                st->humidity_fault = false;
            } else {
                st->humidity_fault = true;
            }
            app_state_unlock();
            if (serr != ESP_OK) {
                ESP_LOGW(TAG, "SHT31 read failed: %s", esp_err_to_name(serr));
            }
        }

        tick++;
        safety_wdt_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensor_task_start(void)
{
    xTaskCreatePinnedToCore(sensor_task, "sensor",
                            SENSOR_TASK_STACK, NULL,
                            SENSOR_TASK_PRIO, &s_handle,
                            SENSOR_TASK_CORE);
}
