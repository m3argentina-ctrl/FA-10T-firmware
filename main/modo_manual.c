#include "modo_manual.h"

#include <string.h>

#include "esp_log.h"
#include "app_config.h"   // SAFETY_HYSTERESIS_C
#include "app_state.h"
#include "audio.h"
#include "telemetry.h"

static const char *TAG = "modo_manual";

// Acumulador fraccional de segundos. Sin esto, dt_s ≈ 0.2 truncaba a 0 al
// castear a uint32 y session_elapsed_s nunca avanzaba.
static float s_elapsed_frac;

esp_err_t modo_manual_start(float setpoint_c, uint32_t duration_s, float hum_target_pct)
{
    if (setpoint_c < 1.0f || duration_s == 0) return ESP_ERR_INVALID_ARG;

    app_state_lock();
    app_state_t *st = app_state_get();
    st->op_mode             = OP_MODE_MANUAL;
    st->run_state           = RUN_STATE_RUNNING;
    st->etapa_activa        = 0;
    st->etapa_sp[0]         = setpoint_c;
    st->etapa_duration_s[0] = duration_s;
    // Limpiar las etapas 2-3: pueden retener valores de una receta anterior y
    // el snapshot de recovery las guarda todas — al reanudar un MANUAL tras un
    // corte de luz, el total se calcula sumando las 3 (duración fantasma).
    for (int i = 1; i < PROG_STAGE_COUNT; ++i) {
        st->etapa_sp[i]         = 0.0f;
        st->etapa_duration_s[i] = 0;
    }
    st->session_total_s     = duration_s;
    st->session_elapsed_s   = 0;
    st->session_remaining_s = duration_s;
    st->effective_setpoint  = setpoint_c;
    st->fan_command_on      = true;
    st->humidity_target     = hum_target_pct;   // auto-stop por humedad (0 = OFF)
    st->cooling_active      = false;
    st->t_min_sesion        =  999.0f;
    st->t_max_sesion        = -999.0f;
    st->warmup_done         = false;
    st->session_energy_wh   = 0.0f;
    st->session_fan_on_s    = 0.0f;
    s_elapsed_frac          = 0.0f;
    snprintf(st->nombre_programa, PROG_NAME_MAX, "%s", "MANUAL");
    app_state_unlock();

    telemetry_note_session_start();

    if (hum_target_pct > 0.0f)
        ESP_LOGI(TAG, "manual start: SP=%.1f°C dur=%lus (tope) humedad obj=%.0f%%",
                 setpoint_c, (unsigned long)duration_s, hum_target_pct);
    else
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
    // Sesión viva detenida a mano = lote interrumpido (COMPLETED/etc. no cuentan).
    bool was_live = (st->run_state == RUN_STATE_RUNNING ||
                     st->run_state == RUN_STATE_PAUSED);
    st->op_mode            = OP_MODE_IDLE;
    st->run_state          = RUN_STATE_IDLE;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    st->cooling_active     = false;
    st->session_remaining_s = 0;
    app_state_unlock();
    if (was_live) telemetry_note_session_end(false);
    ESP_LOGW(TAG, "manual stopped");
    return ESP_OK;
}

void modo_manual_tick(float dt_s)
{
    bool completed_now = false;

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
            // Sin humedad objetivo: completa al terminar el tiempo. Con humedad
            // objetivo (>0): el tiempo es sólo una guía; el corte lo da
            // humidity_autostop al alcanzar la humedad. Igual hay un TOPE de
            // seguridad (HUM_MODE_MAX_RUN_S) para no correr indefinidamente.
            bool     hum_mode = (st->humidity_target > 0.0f);
            uint32_t limit    = hum_mode ? (uint32_t)HUM_MODE_MAX_RUN_S
                                         : st->session_total_s;
            if (st->session_elapsed_s >= limit) {
                st->session_elapsed_s   = limit;
                st->session_remaining_s = 0;
                st->run_state           = RUN_STATE_COMPLETED;
                st->effective_setpoint  = 0.0f;
                // Enfriamiento post-proceso: el fan sigue ventilando; lo
                // gobierna cooldown_tick().
                st->fan_command_on      = true;
                st->cooling_active      = true;
                completed_now           = true;
                if (hum_mode)
                    ESP_LOGW(TAG, "modo humedad: TOPE de seguridad — COMPLETADO por tiempo");
            } else if (hum_mode && st->session_elapsed_s >= st->session_total_s) {
                // Pasó el tiempo guía: seguimos calentando/ventilando esperando
                // que la humedad baje al objetivo.
                st->session_remaining_s = 0;
            } else {
                st->session_remaining_s = st->session_total_s - st->session_elapsed_s;
            }
        }
        // T min/max se trackean siempre (incluyendo warm-up) para diagnóstico.
        if (t_now < st->t_min_sesion) st->t_min_sesion = t_now;
        if (t_now > st->t_max_sesion) st->t_max_sesion = t_now;
    }
    app_state_unlock();

    // Fuera del lock: audio va por cola, pero telemetry escribe NVS (lento).
    if (completed_now) {
        ESP_LOGI(TAG, "session COMPLETED — sounding alarm");
        audio_alarm_done();
        telemetry_note_session_end(true);
    }
}

bool modo_manual_active(void)
{
    app_state_lock();
    bool act = (app_state_get()->op_mode == OP_MODE_MANUAL);
    app_state_unlock();
    return act;
}
