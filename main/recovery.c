#include "recovery.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "telemetry.h"
#include "modo_manual.h"

static const char *TAG = "recovery";
static const char *NS  = "fa10t_recov";
static const char *KEY = "snap";
static const float RECOVERY_SAVE_INTERVAL_S = 60.0f;

static SemaphoreHandle_t s_mtx;
static float             s_accum_s;
static bool              s_had_snapshot;   // ¿hay un snapshot nuestro vigente en NVS?

esp_err_t recovery_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static esp_err_t read_snap(recovery_snapshot_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*out);
    err = nvs_get_blob(h, KEY, out, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t write_snap(const recovery_snapshot_t *in)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_blob(h, KEY, in, sizeof(*in));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool recovery_has_session(recovery_snapshot_t *out)
{
    recovery_snapshot_t s;
    if (read_snap(&s) != ESP_OK) return false;
    if (!s.valid) return false;
    if (out) *out = s;
    telemetry_log_event(TELEM_EVT_POWER_FAIL,
                        "recovered '%s' @stage %u t=%lus",
                        s.recipe.nombre, s.etapa_activa,
                        (unsigned long)s.session_elapsed_s);
    return true;
}

void recovery_tick(float dt_s)
{
    if (!s_mtx) return;

    // 1. Leer el estado de sesión y armar el snapshot bajo app_state_lock.
    //    "Sesión viva" = corriendo o pausada (si se corta la luz pausado,
    //    igual queremos poder retomar). COMPLETED/IDLE no son recuperables.
    app_state_lock();
    app_state_t *st = app_state_get();
    bool active = (st->op_mode != OP_MODE_IDLE) &&
                  (st->run_state == RUN_STATE_RUNNING ||
                   st->run_state == RUN_STATE_PAUSED);
    recovery_snapshot_t snap = {0};
    if (active) {
        snap.valid             = true;
        snap.op_mode           = st->op_mode;
        snap.etapa_activa      = st->etapa_activa;
        snap.session_elapsed_s = st->session_elapsed_s;
        snap.session_energy_wh = st->session_energy_wh;
        snap.session_fan_on_s  = st->session_fan_on_s;
        snprintf(snap.recipe.nombre, PROG_NAME_MAX, "%s", st->nombre_programa);
        for (int i = 0; i < PROG_STAGE_COUNT; ++i) {
            snap.recipe.etapa_sp[i]         = st->etapa_sp[i];
            snap.recipe.etapa_duration_s[i] = st->etapa_duration_s[i];
        }
        snap.recipe.humedad_objetivo = st->humidity_target;   // preservar auto-stop por humedad
    }
    app_state_unlock();

    // 2. Decidir guardar/limpiar bajo s_mtx (acumulador + flag de presencia).
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_accum_s += dt_s;
    bool interval   = (s_accum_s >= RECOVERY_SAVE_INTERVAL_S);
    if (interval) s_accum_s = 0.0f;
    bool start_edge = active && !s_had_snapshot;   // recién arrancó una sesión
    bool stop_edge  = !active && s_had_snapshot;    // recién terminó / se detuvo
    bool do_save    = active && (start_edge || interval);
    s_had_snapshot  = active;
    xSemaphoreGive(s_mtx);

    // 3. Tocar NVS fuera de todo lock. Guardamos apenas arranca la sesión (para
    //    no perder los primeros 60 s) y luego cada RECOVERY_SAVE_INTERVAL_S.
    //    Al terminar limpio borramos el slot → no se ofrece recuperación.
    if (do_save) {
        write_snap(&snap);
    } else if (stop_edge) {
        recovery_clear();
    }
}

void recovery_clear(void)
{
    recovery_snapshot_t empty = {0};
    write_snap(&empty);
}

esp_err_t recovery_resume_from(const recovery_snapshot_t *snap)
{
    if (!snap || !snap->valid) return ESP_ERR_INVALID_ARG;

    if (snap->op_mode == OP_MODE_MANUAL) {
        uint32_t total = 0;
        for (int i = 0; i < PROG_STAGE_COUNT; ++i)
            total += snap->recipe.etapa_duration_s[i];
        esp_err_t err = modo_manual_start(snap->recipe.etapa_sp[0], total,
                                          snap->recipe.humedad_objetivo);
        if (err != ESP_OK) return err;
    } else if (snap->op_mode == OP_MODE_PROGRAMS) {
        esp_err_t err = programa_start_session(&snap->recipe);
        if (err != ESP_OK) return err;
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    // Fast-forward elapsed counter.
    app_state_lock();
    app_state_t *st = app_state_get();
    st->session_elapsed_s   = snap->session_elapsed_s;
    // Restaurar el consumo acumulado para que el total del proceso siga sumando
    // (modo_manual_start / programa_start_session lo habían reseteado a 0).
    st->session_energy_wh   = snap->session_energy_wh;
    st->session_fan_on_s    = snap->session_fan_on_s;

    // En programas, programa_start_session() reseteó etapa_activa=0 y el
    // setpoint a la etapa 1. Restauramos la etapa donde se cortó la luz y
    // apuntamos el setpoint a ESA etapa. warmup_done quedó en false, así que el
    // equipo vuelve a calentar respetando la temperatura de la etapa correcta
    // antes de seguir contando el tiempo restante.
    if (snap->op_mode == OP_MODE_PROGRAMS) {
        uint8_t stage = snap->etapa_activa;
        if (stage >= PROG_STAGE_COUNT) stage = PROG_STAGE_COUNT - 1;
        st->etapa_activa       = stage;
        st->effective_setpoint = st->etapa_sp[stage];
    }

    if (st->session_elapsed_s >= st->session_total_s) {
        // Defensivo (los snapshots solo se guardan con la sesión viva, así que
        // no debería pasar): si igual llegara completo, NO dejar el setpoint
        // activo — el heater quedaría encendido con la sesión "terminada".
        st->session_remaining_s = 0;
        st->run_state           = RUN_STATE_COMPLETED;
        st->effective_setpoint  = 0.0f;
        st->fan_command_on      = false;
    } else {
        st->session_remaining_s = st->session_total_s - snap->session_elapsed_s;
    }
    app_state_unlock();
    return ESP_OK;
}
