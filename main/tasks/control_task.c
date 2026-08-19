#include "control_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "pid_controller.h"
#include "ssr3ch.h"
#include "safety.h"
#include "modo_manual.h"
#include "programa.h"
#include "telemetry.h"
#include "recovery.h"
#include "humidity_autostop.h"
#include "cooldown.h"
#include "extractor.h"

static const char *TAG = "control_task";

// RES_WATTS_PER_MODULE vive ahora en app_config.h (compartido con el LCD, que
// también necesita la potencia para mostrar los kWh del proceso).

static TaskHandle_t s_handle;
TaskHandle_t control_task_handle(void) { return s_handle; }

static void apply_gains(pid_t *pid, const fa10t_config_t *cfg, float sp)
{
    pid_params_t p = {
        .kp = cfg->kp,
        .ki = cfg->ki,
        .kd = cfg->kd,
        .out_min = 0.0f,
        .out_max = 1.0f,
        .d_filter_alpha = 0.85f,
        .derivative_on_measurement = true,
        .kt = cfg->ki,
    };
    pid_set_params(pid, &p);
    pid_set_setpoint(pid, sp);
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
        .kt = 0.15f,
    };
    pid_init(&pid, &init);

    fa10t_config_t cfg;
    app_state_copy_config(&cfg);

    QueueHandle_t q = app_state_sensor_queue();
    uint64_t last_us = esp_timer_get_time();
    bool was_active = false;

    // Edge counters for SSR cycles (used to feed telemetry)
    bool prev_drv_on = false;
    bool prev_fan_on = false;

    safety_wdt_subscribe();

    while (1) {
        sensor_sample_t sample;
        if (xQueueReceive(q, &sample, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS + 50)) != pdTRUE) {
            sample.fault        = true;
            sample.temperature  = 0.0f;
            sample.timestamp_us = esp_timer_get_time();
        }

        uint64_t now_us = esp_timer_get_time();
        float dt = (now_us - last_us) / 1.0e6f;
        if (dt <= 0.0f || dt > 1.0f) dt = (float)CONTROL_TASK_PERIOD_MS / 1000.0f;
        last_us = now_us;

        // Pull live session targets from app_state (set by modo_manual / programa).
        app_state_copy_config(&cfg);
        float sp_eff;
        bool  fan_on;
        bool  fan_fault;
        float rh;
        bool  rh_fault;
        app_state_lock();
        sp_eff    = app_state_get()->effective_setpoint;
        fan_on    = app_state_get()->fan_command_on;
        fan_fault = app_state_get()->fan_fault;
        rh        = app_state_get()->humidity;
        rh_fault  = app_state_get()->humidity_fault;
        app_state_unlock();

        const bool now_active = (sp_eff > 0.5f);
        if (now_active && !was_active) {
            pid_reset(&pid);
        }
        was_active = now_active;

        apply_gains(&pid, &cfg, sp_eff);

        float out;
        if (sample.fault || !now_active) {
            pid_reset(&pid);
            out = 0.0f;
        } else {
            out = pid_compute(&pid, sample.temperature, dt);
        }

        uint32_t faults = safety_evaluate(sample.temperature, sample.limit_temperature,
                                          out, dt, sample.fault, fan_fault);

        if (safety_consume_recovery_event()) {
            pid_reset(&pid);
            ESP_LOGI(TAG, "PID reset after safety recovery");
        }

        const bool tripped = (faults & SAFETY_TRIP_MASK) != 0;
        if (!tripped) {
            const float ramp = safety_recovery_factor();
            ssr3ch_set_duty(SSR_CH_DRV, now_active ? out * ramp : 0.0f);
            ssr3ch_set_duty(SSR_CH_FAN, fan_on     ? 1.0f       : 0.0f);
            // AUX = extractor por humedad (histéresis + anti-cycling + fail-safe).
            ssr3ch_set_duty(SSR_CH_AUX, extractor_tick(rh, rh_fault, now_active, dt));
        }

        // Edge counting for telemetry (off→on transitions)
        const bool drv_now = ssr3ch_get_duty(SSR_CH_DRV) > 0.05f;
        const bool fan_now = ssr3ch_get_duty(SSR_CH_FAN) > 0.05f;
        if (drv_now && !prev_drv_on) telemetry_note_ssr_edge(0);
        if (fan_now && !prev_fan_on) telemetry_note_ssr_edge(1);
        prev_drv_on = drv_now;
        prev_fan_on = fan_now;

        // Falla de turbina: contar una vez por episodio (flanco de subida).
        // Hoy inerte (ACS712 deshabilitado publica fan_fault=false); queda
        // enganchado para cuando el CT mida corriente real.
        static bool prev_fan_fault = false;
        if (fan_fault && !prev_fan_fault) telemetry_note_fan_fault();
        prev_fan_fault = fan_fault;

        if (!sample.fault) telemetry_note_temperature(sample.temperature);

        // Drive session state machines.
        modo_manual_tick(dt);
        programa_tick(dt);

        // Auto-stop por humedad: si la sesión tiene objetivo (>0) y la humedad
        // bajó al objetivo de forma sostenida, completa el proceso. No-op si la
        // sesión corre por tiempo (objetivo = 0). Corre después de los ticks.
        humidity_autostop_tick(dt);

        // Enfriamiento post-proceso: supervisa la ventilación tras COMPLETED y
        // corta el fan al llegar al objetivo de temperatura (o tope). No-op fuera
        // de esa fase.
        cooldown_tick(dt);

        // Persistir la sesión para power-fail recovery (guarda al arrancar la
        // sesión + cada ~60 s; limpia el slot NVS al terminar limpio).
        recovery_tick(dt);

        // Publish duty mirror + clear running flag when tripped.
        app_state_lock();
        app_state_t *st = app_state_get();
        st->pid_output    = out;
        st->ssr_drv_duty  = ssr3ch_get_duty(SSR_CH_DRV);
        st->ssr_fan_duty  = ssr3ch_get_duty(SSR_CH_FAN);
        st->ssr_aux_duty  = ssr3ch_get_duty(SSR_CH_AUX);
        st->safety_faults = faults;
        st->setpoint      = sp_eff;
        st->running       = !tripped && now_active;
        if (tripped && st->run_state != RUN_STATE_ALARM) {
            st->run_state = RUN_STATE_ALARM;
        }
        // Integrar consumo SOLO mientras la sesión corre (incluye calentamiento).
        // Pausa/alarma/completado/idle no suman: el duty ya es 0 o el estado no es
        // RUNNING. Valores por módulo (×Nº módulos en la nube). Acumulador float.
        if (st->run_state == RUN_STATE_RUNNING) {
            st->session_energy_wh += RES_WATTS_PER_MODULE * st->ssr_drv_duty * dt / 3600.0f;
            if (st->ssr_fan_duty > 0.5f) st->session_fan_on_s += dt;
        }
        app_state_unlock();

        safety_wdt_feed();
    }
}

void control_task_start(void)
{
    xTaskCreatePinnedToCore(control_task, "control",
                            CONTROL_TASK_STACK, NULL,
                            CONTROL_TASK_PRIO, &s_handle,
                            CONTROL_TASK_CORE);
}
