#include "programa.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"

#include "app_config.h"   // SAFETY_HYSTERESIS_C
#include "audio.h"

static const char *TAG = "programa";

// Acumulador fraccional de segundos (ver modo_manual.c).
static float s_elapsed_frac;
static const char *NS  = "fa10t_prog";

esp_err_t programa_init(void)
{
    // No state to keep in RAM — slots live in NVS only.
    return ESP_OK;
}

static void slot_key(uint8_t slot, char out[8])
{
    snprintf(out, 8, "p%02u", (unsigned)slot);
}

esp_err_t programa_load(uint8_t slot, programa_t *out)
{
    if (slot >= PROGRAMA_SLOTS || !out) return ESP_ERR_INVALID_ARG;
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
    st->t_min_sesion        =  999.0f;
    st->t_max_sesion        = -999.0f;
    st->warmup_done         = false;
    s_elapsed_frac          = 0.0f;
    snprintf(st->nombre_programa, PROG_NAME_MAX, "%s", p->nombre);
    app_state_unlock();

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
    st->op_mode            = OP_MODE_IDLE;
    st->run_state          = RUN_STATE_IDLE;
    st->effective_setpoint = 0.0f;
    st->fan_command_on     = false;
    app_state_unlock();
    ESP_LOGW(TAG, "programs stopped");
    return ESP_OK;
}

void programa_tick(float dt_s)
{
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

    if (st->session_elapsed_s >= st->session_total_s) {
        st->run_state           = RUN_STATE_COMPLETED;
        st->session_elapsed_s   = st->session_total_s;
        st->session_remaining_s = 0;
        st->effective_setpoint  = 0.0f;
        st->fan_command_on      = false;
        ESP_LOGI(TAG, "session '%s' COMPLETED — sounding alarm", st->nombre_programa);
        audio_alarm_done();
    } else {
        if (new_stage != st->etapa_activa) {
            ESP_LOGI(TAG, "stage transition %u → %u, SP %.1f → %.1f",
                     st->etapa_activa, new_stage,
                     st->etapa_sp[st->etapa_activa], st->etapa_sp[new_stage]);
            st->etapa_activa = new_stage;
        }
        st->effective_setpoint  = st->etapa_sp[new_stage];
        st->session_remaining_s = st->session_total_s - st->session_elapsed_s;
    }
    app_state_unlock();
}

bool programa_active(void)
{
    app_state_lock();
    bool act = (app_state_get()->op_mode == OP_MODE_PROGRAMS);
    app_state_unlock();
    return act;
}
