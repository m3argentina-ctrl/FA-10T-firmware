#include "modo_manual.h"

#include <string.h>

#include "esp_log.h"
#include "app_config.h"   // SAFETY_HYSTERESIS_C
#include "app_state.h"
#include "audio.h"

static const char *TAG = "modo_manual";

// Acumulador fraccional de segundos. Sin esto, dt_s ≈ 0.2 truncaba a 0 al
// castear a uint32 y session_elapsed_s nunca avanzaba.
static float s_elapsed_frac;

esp_err_t modo_manual_start(float setpoint_c, uint32_t duration_s)
{
    if (setpoint_c < 1.0f || duration_s == 0) return ESP_ERR_INVALID_ARG;

    app_state_lock();
    app_state_t *st = app_state_get();
    st->op_mode             = OP_MODE_MANUAL;
    st->run_state           = RUN_STATE_RUNNING;
    st->etapa_activa        = 0;
    st->etapa_sp[0]         = setpoint_c;
    st->etapa_duration_s[0] = duration_s;
    st->session_total_s     = duration_s;
    st->session_elapsed_s   = 0;
    st->session_remaining_s = duration_s;
    st->effective_setpoint  = setpoint_c;
    st->fan_command_on      = true;
    st->t_min_sesion        =  999.0f;
    st->t_max_sesion        = -999.0f;
    st->warmup_done         = false;
    s_elapsed_frac          = 0.0f;
    snprintf(st->nombre_programa, PROG_NAME_MAX, "%s", "MANUAL");
    app_state_unlock();

    ESP_LOGI(TAG, "manual start: SP=%.1f°C duration=%lus", setpoint_c, (unsigned long)duration_s);
    return ESP_OK;
}

esp_err_t modo_manual_pause(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_MANUAL || st->run_state != RUN_STATE_RUNNING) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    st->run_state          = RUN_STATE_PAUSED;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    app_state_unlock();
    ESP_LOGI(TAG, "manual paused");
    return ESP_OK;
}

esp_err_t modo_manual_resume(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_MANUAL || st->run_state != RUN_STATE_PAUSED) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    st->run_state          = RUN_STATE_RUNNING;
    st->effective_setpoint = st->etapa_sp[0];
    st->fan_command_on     = true;
    app_state_unlock();
    ESP_LOGI(TAG, "manual resumed");
    return ESP_OK;
}

esp_err_t modo_manual_stop(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_MANUAL) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    st->op_mode            = OP_MODE_IDLE;
    st->run_state          = RUN_STATE_IDLE;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    st->session_remaining_s = 0;
    app_state_unlock();
    ESP_LOGW(TAG, "manual stopped");
    return ESP_OK;
}

void modo_manual_tick(float dt_s)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    bool active   = (st->op_mode == OP_MODE_MANUAL);
    bool running  = active && (st->run_state == RUN_STATE_RUNNING);
    float t_now   = st->last_sample.temperature;

    if (running) {
        // Warm-up: el tiempo de proceso solo arranca cuando alcanzamos el SP
        // (con histéresis para evitar oscilación). Mientras está calentando,
        // session_elapsed_s queda en 0 y la UI muestra "CALENTANDO".
        if (!st->warmup_done && t_now >= st->effective_setpoint - WARMUP_TOLERANCE_C) {
            st->warmup_done = true;
            ESP_LOGI(TAG, "warm-up done at T=%.1f°C (SP=%.1f°C)",
                     t_now, st->effective_setpoint);
        }

        if (st->warmup_done) {
            s_elapsed_frac += dt_s;
            uint32_t whole = (uint32_t)s_elapsed_frac;
            if (whole > 0) {
                st->session_elapsed_s += whole;
                s_elapsed_frac        -= (float)whole;
            }
            if (st->session_elapsed_s >= st->session_total_s) {
                st->session_elapsed_s   = st->session_total_s;
                st->session_remaining_s = 0;
                st->run_state           = RUN_STATE_COMPLETED;
                st->effective_setpoint  = 0.0f;
                st->fan_command_on      = false;
                ESP_LOGI(TAG, "session COMPLETED — sounding alarm");
                audio_alarm_done();
            } else {
                st->session_remaining_s = st->session_total_s - st->session_elapsed_s;
            }
        }
        // T min/max se trackean siempre (incluyendo warm-up) para diagnóstico.
        if (t_now < st->t_min_sesion) st->t_min_sesion = t_now;
        if (t_now > st->t_max_sesion) st->t_max_sesion = t_now;
    }
    app_state_unlock();
}

bool modo_manual_active(void)
{
    app_state_lock();
    bool act = (app_state_get()->op_mode == OP_MODE_MANUAL);
    app_state_unlock();
    return act;
}
