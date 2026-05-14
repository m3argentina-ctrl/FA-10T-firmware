#include "safety.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#include "ssr_driver.h"

static const char *TAG = "safety";

typedef struct {
    safety_config_t cfg;
    uint32_t        faults;       // current snapshot
    uint32_t        latched;      // sticky bits, only cleared via safety_request_clear()

    // Runaway window tracking
    float    runaway_elapsed_s;
    float    runaway_start_temp;
    bool     runaway_armed;

    // Recovery / soft-start
    bool     recovery_event_pending;
    int64_t  recovery_start_us;       // <= 0 when not in ramp
} safety_state_t;

static safety_state_t s;

esp_err_t safety_init(const safety_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    const uint32_t idle_mask = (1u << 0) | (1u << 1);
#else
    const uint32_t idle_mask = (1u << 0);
#endif
    esp_task_wdt_config_t wdt = {
        .timeout_ms     = cfg->wdt_timeout_s * 1000,
        .idle_core_mask = idle_mask,
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

    ESP_LOGI(TAG, "safety init: Tmax=%.1f°C hyst=%.1f°C runaway ΔT<%.2f°C in %.0fs ramp=%.1fs WDT=%lus",
             cfg->temp_max_c, cfg->hysteresis_c,
             cfg->runaway_dt_c, cfg->runaway_window_s,
             cfg->recovery_ramp_s,
             (unsigned long)cfg->wdt_timeout_s);
    return ESP_OK;
}

esp_err_t safety_wdt_subscribe(void)   { return esp_task_wdt_add(NULL); }
esp_err_t safety_wdt_feed(void)        { return esp_task_wdt_reset();   }
esp_err_t safety_wdt_unsubscribe(void) { return esp_task_wdt_delete(NULL); }

static void trip(uint32_t fault, const char *reason, bool latch)
{
    if ((s.faults & fault) == 0) {
        ESP_LOGE(TAG, "SAFETY TRIP: %s (0x%02lX)%s",
                 reason, (unsigned long)fault, latch ? " [LATCHED]" : "");
    }
    s.faults |= fault;
    if (latch) s.latched |= fault;
    // Cancel any in-flight recovery ramp; we just tripped again.
    s.recovery_start_us = 0;
    ssr_driver_force_off();
}

static void start_recovery_ramp(void)
{
    s.recovery_event_pending = true;
    s.recovery_start_us      = esp_timer_get_time();
    ssr_driver_enable();
    ESP_LOGI(TAG, "recovery: SSR re-enabled, soft-start ramp %.1fs", s.cfg.recovery_ramp_s);
}

uint32_t safety_evaluate(float temperature, float duty, float dt_s, bool sensor_fault)
{
    // Non-latched bits get re-evaluated each cycle; latched bits stay sticky
    // until safety_request_clear() accepts a clear.
    s.faults = 0;

    if (sensor_fault) {
        // Sensor faults are treated as transient — they can clear themselves
        // when the sensor recovers. The control loop already forces duty=0 in
        // this case; we still cut the SSR for redundancy.
        trip(SAFETY_SENSOR_FAULT, "sensor fault", false);
    }

    if (!sensor_fault && temperature > s.cfg.temp_max_c) {
        trip(SAFETY_OVERTEMP, "over-temperature", true);
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
                    trip(SAFETY_RUNAWAY, "no rise under heat call", true);
                }
                // Rearm for next window
                s.runaway_elapsed_s  = 0.0f;
                s.runaway_start_temp = temperature;
            }
        }
    } else {
        s.runaway_armed = false;
    }

    // Auto-recovery only when nothing is currently faulted AND nothing is
    // latched. Latched faults need an explicit clear via safety_request_clear().
    if (s.faults == 0 && s.latched == 0 && !ssr_driver_is_enabled()) {
        start_recovery_ramp();
    }

    return s.faults | s.latched;
}

uint32_t safety_get_faults(void) { return s.faults | s.latched; }

bool safety_request_clear(float current_temp)
{
    if (s.latched == 0) {
        ESP_LOGI(TAG, "clear requested but no latched faults");
        return true;
    }

    if (s.latched & SAFETY_OVERTEMP) {
        const float allow_below = s.cfg.temp_max_c - s.cfg.hysteresis_c;
        if (current_temp >= allow_below) {
            ESP_LOGW(TAG, "clear REJECTED: T=%.1f°C still ≥ %.1f°C (limit %.1f - hyst %.1f)",
                     current_temp, allow_below,
                     s.cfg.temp_max_c, s.cfg.hysteresis_c);
            return false;
        }
    }
    if (s.latched & SAFETY_RUNAWAY) {
        ESP_LOGW(TAG, "clearing RUNAWAY latch on operator ack (T=%.1f°C)", current_temp);
    }

    s.latched              = 0;
    s.faults               = 0;
    s.runaway_armed        = false;
    s.runaway_elapsed_s    = 0.0f;
    ESP_LOGI(TAG, "safety latch cleared by operator (T=%.1f°C)", current_temp);
    start_recovery_ramp();
    return true;
}

bool safety_consume_recovery_event(void)
{
    bool ev = s.recovery_event_pending;
    s.recovery_event_pending = false;
    return ev;
}

float safety_recovery_factor(void)
{
    if (s.recovery_start_us <= 0 || s.cfg.recovery_ramp_s <= 0.0f) return 1.0f;
    int64_t now = esp_timer_get_time();
    float elapsed_s = (float)(now - s.recovery_start_us) / 1.0e6f;
    if (elapsed_s >= s.cfg.recovery_ramp_s) {
        s.recovery_start_us = 0;
        return 1.0f;
    }
    if (elapsed_s < 0.0f) elapsed_s = 0.0f;
    return elapsed_s / s.cfg.recovery_ramp_s;
}
