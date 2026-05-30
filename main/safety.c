#include "safety.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ssr3ch.h"

static const char *TAG = "safety";

// Mutex que serializa TODO acceso al estado de safety. Es imprescindible
// porque tres tasks distintas lo tocan: control_task (safety_evaluate cada
// 200 ms), watchdog_task (safety_evaluate cuando el sensor queda stale +
// safety_request_clear desde el botón BOOT) y la UI task (safety_request_clear
// desde el botón CONFIRMAR de screen_alarma). Sin esto, safety_evaluate hace
// `s.faults = 0` al entrar y acumula → un clear o un segundo evaluate
// concurrente puede corromper el detector de runaway o pisar un trip.
// Los helpers internos (trip, start_recovery_ramp) NO toman el lock: corren
// siempre dentro de una sección crítica ya tomada por la función pública.
// Orden de locks siempre safety→ssr3ch (trip/recovery llaman a ssr3ch_*);
// ssr3ch nunca llama a safety, así que no hay deadlock.
static SemaphoreHandle_t s_mtx;

static inline void safety_lock(void)   { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
static inline void safety_unlock(void) { if (s_mtx) xSemaphoreGive(s_mtx); }

typedef struct {
    safety_config_t cfg;
    uint32_t        faults;
    uint32_t        latched;

    // Runaway: now triggered by T rising while SSR_DRV is essentially OFF.
    float    runaway_elapsed_s;
    float    runaway_start_temp;
    bool     runaway_armed;

    bool     recovery_event_pending;
    int64_t  recovery_start_us;
} safety_state_t;

static safety_state_t s;

esp_err_t safety_init(const safety_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;

    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        if (!s_mtx) {
            ESP_LOGE(TAG, "safety mutex alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }

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
    esp_err_t err = esp_task_wdt_reconfigure(&wdt);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_init(&wdt);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TWDT configure: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "safety init: Tmax=%.1f°C hyst=%.1f°C runaway ΔT>%.2f°C in %.0fs ramp=%.1fs WDT=%lus",
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
    s.recovery_start_us = 0;
    ssr3ch_force_all_off();
}

static void start_recovery_ramp(void)
{
    s.recovery_event_pending = true;
    s.recovery_start_us      = esp_timer_get_time();
    ssr3ch_enable();
    ESP_LOGI(TAG, "recovery: SSRs re-enabled, soft-start ramp %.1fs", s.cfg.recovery_ramp_s);
}

uint32_t safety_evaluate(float temperature, float duty, float dt_s,
                         bool sensor_fault, bool fan_fault)
{
    safety_lock();
    s.faults = 0;

    if (sensor_fault) {
        trip(SAFETY_SENSOR_FAULT, "sensor fault", false);
    }

    if (fan_fault) {
        // Latch: a turbine fault must be acknowledged. Without airflow the
        // heater is unsafe regardless of temperature reading.
        trip(SAFETY_FAN_FAULT, "turbine current below nominal", true);
    }

    if (!sensor_fault && temperature > s.cfg.temp_max_c) {
        trip(SAFETY_OVERTEMP, "over-temperature", true);
    } else if (!sensor_fault &&
               temperature >= (s.cfg.temp_max_c - s.cfg.hysteresis_c)) {
        s.faults |= SAFETY_WARN_NEAR_LIMIT;
    }

    // Runaway: with the heater SSR essentially OFF, T must NOT rise faster
    // than runaway_dt_c within runaway_window_s. This catches a shorted SSR
    // or stuck-on heater that the controller can no longer modulate.
    if (!sensor_fault && duty <= s.cfg.runaway_duty_thr) {
        if (!s.runaway_armed) {
            s.runaway_armed      = true;
            s.runaway_elapsed_s  = 0.0f;
            s.runaway_start_temp = temperature;
        } else {
            s.runaway_elapsed_s += dt_s;
            float delta = temperature - s.runaway_start_temp;
            if (delta > s.cfg.runaway_dt_c) {
                trip(SAFETY_RUNAWAY, "rising temp with SSR off", true);
            }
            if (s.runaway_elapsed_s >= s.cfg.runaway_window_s) {
                s.runaway_elapsed_s  = 0.0f;
                s.runaway_start_temp = temperature;
            }
        }
    } else {
        s.runaway_armed = false;
    }

    if ((s.faults & SAFETY_TRIP_MASK) == 0 &&
        (s.latched & SAFETY_TRIP_MASK) == 0 &&
        !ssr3ch_is_enabled()) {
        start_recovery_ramp();
    }

    uint32_t result = s.faults | s.latched;
    safety_unlock();
    return result;
}

uint32_t safety_get_faults(void)
{
    safety_lock();
    uint32_t f = s.faults | s.latched;
    safety_unlock();
    return f;
}

bool safety_request_clear(float current_temp)
{
    safety_lock();

    if (s.latched == 0) {
        ESP_LOGI(TAG, "clear requested but no latched faults");
        safety_unlock();
        return true;
    }

    if (s.latched & SAFETY_OVERTEMP) {
        const float allow_below = s.cfg.temp_max_c - s.cfg.hysteresis_c;
        if (current_temp >= allow_below) {
            ESP_LOGW(TAG, "clear REJECTED: T=%.1f°C still ≥ %.1f°C (limit %.1f - hyst %.1f)",
                     current_temp, allow_below,
                     s.cfg.temp_max_c, s.cfg.hysteresis_c);
            safety_unlock();
            return false;
        }
    }
    if (s.latched & SAFETY_RUNAWAY) {
        ESP_LOGW(TAG, "clearing RUNAWAY latch on operator ack (T=%.1f°C)", current_temp);
    }
    if (s.latched & SAFETY_FAN_FAULT) {
        ESP_LOGW(TAG, "clearing FAN_FAULT latch on operator ack");
    }

    s.latched              = 0;
    s.faults               = 0;
    s.runaway_armed        = false;
    s.runaway_elapsed_s    = 0.0f;
    ESP_LOGI(TAG, "safety latch cleared by operator (T=%.1f°C)", current_temp);
    start_recovery_ramp();
    safety_unlock();
    return true;
}

bool safety_consume_recovery_event(void)
{
    safety_lock();
    bool ev = s.recovery_event_pending;
    s.recovery_event_pending = false;
    safety_unlock();
    return ev;
}

float safety_recovery_factor(void)
{
    safety_lock();
    float factor;
    if (s.recovery_start_us <= 0 || s.cfg.recovery_ramp_s <= 0.0f) {
        factor = 1.0f;
    } else {
        int64_t now = esp_timer_get_time();
        float elapsed_s = (float)(now - s.recovery_start_us) / 1.0e6f;
        if (elapsed_s >= s.cfg.recovery_ramp_s) {
            s.recovery_start_us = 0;
            factor = 1.0f;
        } else {
            if (elapsed_s < 0.0f) elapsed_s = 0.0f;
            factor = elapsed_s / s.cfg.recovery_ramp_s;
        }
    }
    safety_unlock();
    return factor;
}
