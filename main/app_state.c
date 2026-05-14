#include "app_state.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "app_state";

static app_state_t      s_state;
static fa10t_config_t   s_config;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t    s_sensor_q;

void app_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_mtx = xSemaphoreCreateMutex();
    // Depth 1 + xQueueOverwrite from the producer keeps the consumer (control
    // loop) on the freshest sample without the racy drop-then-resend pattern.
    s_sensor_q = xQueueCreate(1, sizeof(sensor_sample_t));
    if (!s_mtx || !s_sensor_q) {
        ESP_LOGE(TAG, "primitives alloc failed");
        abort();
    }
}

QueueHandle_t app_state_sensor_queue(void) { return s_sensor_q; }
void app_state_lock(void)                  { xSemaphoreTake(s_mtx, portMAX_DELAY); }
void app_state_unlock(void)                { xSemaphoreGive(s_mtx); }
app_state_t *app_state_get(void)           { return &s_state; }

void app_state_copy_config(fa10t_config_t *out)
{
    if (!out) return;
    app_state_lock();
    *out = s_config;
    app_state_unlock();
}

void app_state_set_config(const fa10t_config_t *cfg)
{
    if (!cfg) return;
    app_state_lock();
    s_config = *cfg;
    s_state.setpoint = cfg->setpoint;
    app_state_unlock();
}
