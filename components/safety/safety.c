#include "safety.h"

#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "ssr_driver.h"

static const char *TAG = "safety";

typedef struct {
    safety_config_t cfg;
    uint32_t        faults;       // current snapshot
    uint32_t        latched;      // sticky bits since last clear

    // Runaway window tracking
    float    runaway_elapsed_s;
    float    runaway_start_temp;
    bool     runaway_armed;
} safety_state_t;

static safety_state_t s;

esp_err_t safety_init(const safety_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;

    esp_task_wdt_config_t wdt = {
        .timeout_ms     = cfg->wdt_timeout_s * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic  = true,
    };
    // Reconfigure existing TWDT (created by IDF) instead of init, to honor sdkconfig.
    esp_err_t err = esp_task_wdt_reconfigure(&wdt);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_init(&wdt);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TWDT configure: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "safety init: Tmax=%.1f°C runaway ΔT<%.2f°C in %.0fs WDT=%lus",
             cfg->temp_max_c, cfg->runaway_dt_c, cfg->runaway_window_s,
             (unsigned long)cfg->wdt_timeout_s);
    return ESP_OK;
}

esp_err_t safety_wdt_subscribe(void)   { return esp_task_wdt_add(NULL); }
esp_err_t safety_wdt_feed(void)        { return esp_task_wdt_reset();   }
esp_err_t safety_wdt_unsubscribe(void) { return esp_task_wdt_delete(NULL); }

static void trip(uint32_t fault, const char *reason)
{
    if ((s.faults & fault) == 0) {
        ESP_LOGE(TAG, "SAFETY TRIP: %s (0x%02lX)", reason, (unsigned long)fault);
    }
    s.faults  |= fault;
    s.latched |= fault;
    ssr_driver_force_off();
}

uint32_t safety_evaluate(float temperature, float duty, float dt_s, bool sensor_fault)
{
    // Clear non-latched bits each cycle; latched stays sticky.
    s.faults = 0;

    if (sensor_fault) {
        trip(SAFETY_SENSOR_FAULT, "sensor fault");
    }

    if (!sensor_fault && temperature > s.cfg.temp_max_c) {
        trip(SAFETY_OVERTEMP, "over-temperature");
    }

    // Thermal runaway: while calling for substantial heat, ΔT must rise enough
    // within the observation window. Otherwise something is wrong (sensor stuck,
    // heater disconnected, SSR shorted off, etc).
    if (!sensor_fault && duty >= s.cfg.runaway_duty_thr) {
        if (!s.runaway_armed) {
            s.runaway_armed       = true;
            s.runaway_elapsed_s   = 0.0f;
            s.runaway_start_temp  = temperature;
        } else {
            s.runaway_elapsed_s += dt_s;
            if (s.runaway_elapsed_s >= s.cfg.runaway_window_s) {
                float delta = temperature - s.runaway_start_temp;
                if (delta < s.cfg.runaway_dt_c) {
                    trip(SAFETY_RUNAWAY, "no rise under heat call");
                }
                // Rearm for next window
                s.runaway_elapsed_s  = 0.0f;
                s.runaway_start_temp = temperature;
            }
        }
    } else {
        s.runaway_armed = false;
    }

    // Re-enable SSR only when *no* latched faults remain
    if (s.latched == 0 && !ssr_driver_is_enabled()) {
        ssr_driver_enable();
    }
    return s.faults | s.latched;
}

uint32_t safety_get_faults(void)   { return s.faults | s.latched; }
void safety_clear_latched(void)    { s.latched = 0; }
