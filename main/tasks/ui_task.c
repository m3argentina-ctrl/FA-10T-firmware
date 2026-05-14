#include "ui_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "app_state.h"
#include "display.h"

static const char *TAG = "ui_task";

static void ui_task(void *arg)
{
    (void)arg;
    display_build_main_screen();

    uint32_t status_counter = 0;
    while (1) {
        if (display_lvgl_lock(20) == ESP_OK) {
            lv_timer_handler();
            display_lvgl_unlock();
        }

        // Refresh status text/bar at ~5 Hz to avoid label thrashing
        if (++status_counter >= (200 / UI_TASK_PERIOD_MS)) {
            status_counter = 0;
            display_status_t st;
            app_state_lock();
            const app_state_t *as = app_state_get();
            st.temperature    = as->last_sample.temperature;
            st.setpoint       = as->setpoint;
            st.ssr_duty       = as->ssr_duty;
            st.sensor_fault   = as->last_sample.fault;
            st.runaway        = (as->safety_faults & 0x04) != 0; // SAFETY_RUNAWAY
            st.safety_tripped = (as->safety_faults != 0);
            st.uptime_s       = as->uptime_s;
            app_state_unlock();
            display_update_status(&st);
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
    }
}

void ui_task_start(void)
{
    xTaskCreatePinnedToCore(ui_task, "ui",
                            UI_TASK_STACK, NULL,
                            UI_TASK_PRIO, NULL,
                            UI_TASK_CORE);
    ESP_LOGI(TAG, "UI task started");
}
