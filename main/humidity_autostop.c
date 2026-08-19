#include "humidity_autostop.h"

#include "esp_log.h"

#include "app_config.h"
#include "app_state.h"
#include "audio.h"
#include "telemetry.h"

static const char *TAG = "hum_auto";

// Estado interno (vive entre ciclos; se resetea solo cuando no hay sesión auto).
static float s_below_s;       // humedad <= objetivo sostenida (s)
static float s_fault_s;       // falla de sensor sostenida (s)
static bool  s_fault_noted;   // ya avisamos la falla de este episodio

// Completa la sesión activa: COMPLETADO + arranca el enfriamiento post-proceso.
// Revalida bajo lock (el estado pudo cambiar entre la lectura y este commit).
// Devuelve true si se aplicó.
static bool complete_session(void)
{
    app_state_lock();
    app_state_t *s = app_state_get();
    bool ok = (s->op_mode != OP_MODE_IDLE) && (s->run_state == RUN_STATE_RUNNING);
    if (ok) {
        s->run_state           = RUN_STATE_COMPLETED;
        s->session_remaining_s = 0;
        s->effective_setpoint  = 0.0f;
        // Enfriamiento post-proceso: el fan sigue; lo gobierna cooldown_tick().
        s->fan_command_on      = true;
        s->cooling_active      = true;
    }
    app_state_unlock();
    return ok;
}

void humidity_autostop_tick(float dt_s)
{
    // 1. Leer el estado bajo lock y soltar antes de decidir / avisar.
    app_state_lock();
    app_state_t *st = app_state_get();
    bool     running = (st->op_mode != OP_MODE_IDLE) &&
                       (st->run_state == RUN_STATE_RUNNING);
    float    target  = st->humidity_target;
    float    hum     = st->humidity;
    bool     hum_flt = st->humidity_fault;
    uint32_t elapsed = st->session_elapsed_s;
    uint32_t total   = st->session_total_s;
    app_state_unlock();

    // 2. Sin sesión corriendo o sin objetivo => modo por tiempo: no-op + reset.
    if (!running || target <= 0.0f) {
        s_below_s = 0.0f;
        s_fault_s = 0.0f;
        s_fault_noted = false;
        return;
    }

    // 3. Sensor en falla: NO decidir con datos malos. Si la falla persiste y ya
    //    pasó el tiempo programado, completar por tiempo (respaldo) para no
    //    correr hasta el tope de 24 h con un sensor muerto.
    if (hum_flt) {
        s_below_s = 0.0f;
        s_fault_s += dt_s;
        if (!s_fault_noted && s_fault_s >= (float)HUM_AUTOSTOP_FAULT_TO_S) {
            s_fault_noted = true;
            ESP_LOGW(TAG, "sensor de humedad sin lectura — respaldo por tiempo programado");
            telemetry_log_event(TELEM_EVT_FAULT, "sensor humedad sin lectura");
        }
        if (s_fault_s >= (float)HUM_AUTOSTOP_FAULT_TO_S && elapsed >= total) {
            if (complete_session()) {
                ESP_LOGW(TAG, "sensor humedad en falla — COMPLETADO por tiempo (respaldo)");
                audio_alarm_done();
                telemetry_log_event(TELEM_EVT_SESSION_END, "fin por tiempo (sensor hum. en falla)");
                telemetry_note_session_end(true);
            }
        }
        return;
    }
    s_fault_s     = 0.0f;
    s_fault_noted = false;

    // 4. Tiempo mínimo inicial: no auto-parar al arranque (la cámara puede estar
    //    seca antes de que el producto largue humedad). session_elapsed_s corre
    //    post-calentamiento, así que esto cuenta desde que arrancó el secado.
    if (elapsed < (uint32_t)HUM_AUTOSTOP_MIN_RUN_S) {
        s_below_s = 0.0f;
        return;
    }

    // 5. Humedad <= objetivo de forma sostenida => completar.
    if (hum <= target) s_below_s += dt_s;
    else               s_below_s  = 0.0f;

    if (s_below_s < (float)HUM_AUTOSTOP_SUSTAIN_S) return;

    s_below_s = 0.0f;
    if (complete_session()) {
        ESP_LOGI(TAG, "humedad %.0f%% <= objetivo %.0f%% sostenida — COMPLETADO", hum, target);
        audio_alarm_done();
        telemetry_log_event(TELEM_EVT_SESSION_END, "humedad obj. %.0f%% alcanzada", hum);
        telemetry_note_session_end(true);   // cuenta el lote como completado
    }
}
