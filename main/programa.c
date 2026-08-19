#include "programa.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"

#include "app_config.h"   // SAFETY_HYSTERESIS_C
#include "audio.h"
#include "telemetry.h"

static const char *TAG = "programa";

// Acumulador fraccional de segundos (ver modo_manual.c).
static float s_elapsed_frac;
static const char *NS  = "fa10t_prog";

// ---------------------------------------------------------------------------
// Recetas de fábrica. Un equipo NUEVO arranca con este set precargado en vez de
// slots vacíos. Se siembran UNA sola vez (flag "seed_ver" en NVS); después el
// usuario tiene control total: si edita o borra una receta, NO se revierte en
// el próximo arranque. Para empujar nuevas recetas por defecto en un firmware
// futuro, subir PROG_SEED_VERSION: sólo se rellenan los slots que sigan VACÍOS,
// nunca se pisa una receta guardada por el usuario.
//
// Restricciones: nombre <= 19 chars · setpoint 20–80 °C · duración en segundos.
// >>> AJUSTAR estos valores a las recetas reales de Bio Origen. <<<
// ---------------------------------------------------------------------------
#define PROG_SEED_VERSION 1
#define SEG_POR_HORA      3600u

typedef struct {
    const char *nombre;
    float       sp[PROG_STAGE_COUNT];      // setpoint de cada etapa (°C)
    uint32_t    dur_s[PROG_STAGE_COUNT];   // duración de cada etapa (s)
} prog_seed_t;

static const prog_seed_t k_seed[] = {
    //   nombre        E1   E2   E3            E1               E2               E3
    { "FRUTAS",     {  60,  55,  50 }, { 2*SEG_POR_HORA, 3*SEG_POR_HORA, 2*SEG_POR_HORA } },
    { "CITRICOS",   {  55,  55,  50 }, { 3*SEG_POR_HORA, 3*SEG_POR_HORA, 3*SEG_POR_HORA } },
    { "VERDURAS",   {  55,  52,  50 }, { 2*SEG_POR_HORA, 3*SEG_POR_HORA, 2*SEG_POR_HORA } },
    { "TOMATE",     {  60,  58,  55 }, { 3*SEG_POR_HORA, 3*SEG_POR_HORA, 3*SEG_POR_HORA } },
    { "HIERBAS",    {  40,  38,  35 }, { 1*SEG_POR_HORA, 2*SEG_POR_HORA, 1*SEG_POR_HORA } },
    { "HONGOS",     {  55,  50,  45 }, { 2*SEG_POR_HORA, 2*SEG_POR_HORA, 2*SEG_POR_HORA } },
};

#undef SEG_POR_HORA

// Siembra las recetas de fábrica si el equipo nunca fue sembrado (o si se subió
// PROG_SEED_VERSION). No pisa slots que el usuario ya guardó.
static void seed_defaults(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t ver = 0;
    nvs_get_u8(h, "seed_ver", &ver);            // ausente => queda en 0
    nvs_close(h);
    if (ver >= PROG_SEED_VERSION) return;       // ya sembrado en esta versión

    const size_t n = sizeof(k_seed) / sizeof(k_seed[0]);
    for (uint8_t slot = 0; slot < n && slot < PROGRAMA_SLOTS; ++slot) {
        programa_t cur;
        if (programa_load(slot, &cur) == ESP_OK && cur.used) continue; // no pisar

        programa_t p;
        memset(&p, 0, sizeof(p));
        p.used = true;
        snprintf(p.nombre, PROG_NAME_MAX, "%s", k_seed[slot].nombre);
        for (int i = 0; i < PROG_STAGE_COUNT; ++i) {
            p.etapa_sp[i]         = k_seed[slot].sp[i];
            p.etapa_duration_s[i] = k_seed[slot].dur_s[i];
        }
        programa_save(slot, &p);
    }

    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "seed_ver", PROG_SEED_VERSION);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "recetas de fábrica sembradas (v%u)", PROG_SEED_VERSION);
    }
}

esp_err_t programa_init(void)
{
    // Slots viven en NVS; sembramos las recetas de fábrica si el equipo es nuevo.
    seed_defaults();
    return ESP_OK;
}

static void slot_key(uint8_t slot, char out[8])
{
    snprintf(out, 8, "p%02u", (unsigned)slot);
}

esp_err_t programa_load(uint8_t slot, programa_t *out)
{
    if (slot >= PROGRAMA_SLOTS || !out) return ESP_ERR_INVALID_ARG;
    // Cero ANTES de leer: si la receta guardada es de un firmware viejo (blob
    // más chico, sin humedad_objetivo), los campos nuevos quedan en 0 (=OFF) en
    // vez de basura. Migración transparente.
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    char key[8]; slot_key(slot, key);
    size_t sz = sizeof(*out);
    err = nvs_get_blob(h, key, out, &sz);
    nvs_close(h);
    if (err != ESP_OK) {
        memset(out, 0, sizeof(*out));
        return err;
    }
    return ESP_OK;
}

esp_err_t programa_save(uint8_t slot, const programa_t *p)
{
    if (slot >= PROGRAMA_SLOTS || !p) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");
    char key[8]; slot_key(slot, key);
    esp_err_t err = nvs_set_blob(h, key, p, sizeof(*p));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "slot %u saved '%s'", slot, p->nombre);
    return err;
}

esp_err_t programa_erase(uint8_t slot)
{
    if (slot >= PROGRAMA_SLOTS) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");
    char key[8]; slot_key(slot, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static uint32_t total_duration(const programa_t *p)
{
    uint32_t t = 0;
    for (int i = 0; i < PROG_STAGE_COUNT; ++i) t += p->etapa_duration_s[i];
    return t;
}

esp_err_t programa_start_session(const programa_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    uint32_t total = total_duration(p);
    if (total == 0) return ESP_ERR_INVALID_ARG;

    app_state_lock();
    app_state_t *st = app_state_get();
    st->op_mode             = OP_MODE_PROGRAMS;
    st->run_state           = RUN_STATE_RUNNING;
    st->etapa_activa        = 0;
    for (int i = 0; i < PROG_STAGE_COUNT; ++i) {
        st->etapa_sp[i]         = p->etapa_sp[i];
        st->etapa_duration_s[i] = p->etapa_duration_s[i];
    }
    st->session_total_s     = total;
    st->session_elapsed_s   = 0;
    st->session_remaining_s = total;
    st->effective_setpoint  = p->etapa_sp[0];
    st->fan_command_on      = true;
    st->humidity_target     = p->humedad_objetivo;   // auto-stop por humedad (0 = OFF)
    st->cooling_active      = false;
    st->t_min_sesion        =  999.0f;
    st->t_max_sesion        = -999.0f;
    st->warmup_done         = false;
    st->session_energy_wh   = 0.0f;
    st->session_fan_on_s    = 0.0f;
    s_elapsed_frac          = 0.0f;
    snprintf(st->nombre_programa, PROG_NAME_MAX, "%s", p->nombre);
    app_state_unlock();

    telemetry_note_session_start();

    ESP_LOGI(TAG, "session start '%s' total=%lus stages=[%.1f/%lus, %.1f/%lus, %.1f/%lus]",
             p->nombre, (unsigned long)total,
             p->etapa_sp[0], (unsigned long)p->etapa_duration_s[0],
             p->etapa_sp[1], (unsigned long)p->etapa_duration_s[1],
             p->etapa_sp[2], (unsigned long)p->etapa_duration_s[2]);
    return ESP_OK;
}

esp_err_t programa_pause(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_PROGRAMS || st->run_state != RUN_STATE_RUNNING) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    st->run_state          = RUN_STATE_PAUSED;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    app_state_unlock();
    ESP_LOGI(TAG, "programs paused");
    return ESP_OK;
}

esp_err_t programa_resume(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_PROGRAMS || st->run_state != RUN_STATE_PAUSED) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    st->run_state          = RUN_STATE_RUNNING;
    st->effective_setpoint = st->etapa_sp[st->etapa_activa];
    st->fan_command_on     = true;
    app_state_unlock();
    ESP_LOGI(TAG, "programs resumed");
    return ESP_OK;
}

esp_err_t programa_stop(void)
{
    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_PROGRAMS) {
        app_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    // Sesión viva detenida a mano = lote interrumpido.
    bool was_live = (st->run_state == RUN_STATE_RUNNING ||
                     st->run_state == RUN_STATE_PAUSED);
    st->op_mode            = OP_MODE_IDLE;
    st->run_state          = RUN_STATE_IDLE;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    st->cooling_active     = false;
    app_state_unlock();
    if (was_live) telemetry_note_session_end(false);
    ESP_LOGW(TAG, "programs stopped");
    return ESP_OK;
}

void programa_tick(float dt_s)
{
    bool completed_now = false;

    app_state_lock();
    app_state_t *st = app_state_get();
    if (st->op_mode != OP_MODE_PROGRAMS || st->run_state != RUN_STATE_RUNNING) {
        app_state_unlock();
        return;
    }
    float t_now = st->last_sample.temperature;
    if (t_now < st->t_min_sesion) st->t_min_sesion = t_now;
    if (t_now > st->t_max_sesion) st->t_max_sesion = t_now;

    // Warm-up: el tiempo no avanza hasta que T cruce el SP de la etapa ACTUAL.
    // En un arranque normal etapa_activa==0, así que espera la etapa 1. Tras un
    // corte de luz se recupera en la etapa donde estaba (2 o 3), y entonces el
    // warm-up debe esperar el SP de ESA etapa, no el de la etapa 1. Una vez hecho
    // el warm-up, la sesión corre normal (cada etapa dura lo que dura, no
    // esperamos nuevamente el SP entre etapas).
    if (!st->warmup_done &&
        t_now >= st->etapa_sp[st->etapa_activa] - WARMUP_TOLERANCE_C) {
        st->warmup_done = true;
        ESP_LOGI(TAG, "warm-up done at T=%.1f°C (SP etapa %u=%.1f°C)",
                 t_now, (unsigned)(st->etapa_activa + 1),
                 st->etapa_sp[st->etapa_activa]);
    }
    if (!st->warmup_done) {
        app_state_unlock();
        return;
    }

    s_elapsed_frac += dt_s;
    uint32_t whole = (uint32_t)s_elapsed_frac;
    if (whole > 0) {
        st->session_elapsed_s += whole;
        s_elapsed_frac        -= (float)whole;
    }

    // Compute which stage we're in based on cumulative duration.
    uint32_t acc = 0;
    uint8_t  new_stage = PROG_STAGE_COUNT - 1;
    for (uint8_t i = 0; i < PROG_STAGE_COUNT; ++i) {
        acc += st->etapa_duration_s[i];
        if (st->session_elapsed_s < acc) { new_stage = i; break; }
    }

    // Sin humedad objetivo: completa al terminar la receta. Con humedad objetivo
    // (>0): la receta corre sus etapas por tiempo, pero al llegar al final NO
    // completa: mantiene la última etapa hasta que la humedad baje al objetivo
    // (lo corta humidity_autostop). Tope absoluto de seguridad igual.
    bool     hum_mode = (st->humidity_target > 0.0f);
    uint32_t limit    = hum_mode ? (uint32_t)HUM_MODE_MAX_RUN_S : st->session_total_s;
    if (st->session_elapsed_s >= limit) {
        st->run_state           = RUN_STATE_COMPLETED;
        st->session_elapsed_s   = limit;
        st->session_remaining_s = 0;
        st->effective_setpoint  = 0.0f;
        // Enfriamiento post-proceso: fan sigue; lo gobierna cooldown_tick().
        st->fan_command_on      = true;
        st->cooling_active      = true;
        completed_now           = true;
        if (hum_mode)
            ESP_LOGW(TAG, "modo humedad: TOPE de seguridad — COMPLETADO por tiempo");
    } else {
        // Avanzar/mantener etapa. Con humedad objetivo, tras pasar el tiempo de
        // la receta new_stage queda en la última etapa (el for no rompe) →
        // mantiene el último setpoint mientras esperamos que baje la humedad.
        if (new_stage != st->etapa_activa) {
            ESP_LOGI(TAG, "stage transition %u → %u, SP %.1f → %.1f",
                     st->etapa_activa, new_stage,
                     st->etapa_sp[st->etapa_activa], st->etapa_sp[new_stage]);
            st->etapa_activa = new_stage;
        }
        st->effective_setpoint  = st->etapa_sp[new_stage];
        if (hum_mode && st->session_elapsed_s >= st->session_total_s)
            st->session_remaining_s = 0;
        else
            st->session_remaining_s = st->session_total_s - st->session_elapsed_s;
    }
    app_state_unlock();

    // Fuera del lock: telemetry escribe NVS (lento); audio va por cola.
    if (completed_now) {
        ESP_LOGI(TAG, "session COMPLETED — sounding alarm");
        audio_alarm_done();
        telemetry_note_session_end(true);
    }
}

bool programa_active(void)
{
    app_state_lock();
    bool act = (app_state_get()->op_mode == OP_MODE_PROGRAMS);
    app_state_unlock();
    return act;
}
