#include "cooldown.h"

#include "esp_log.h"

#include "app_config.h"
#include "app_state.h"

static const char *TAG = "cooldown";

static float s_elapsed_s;    // tiempo en fase de enfriamiento
static bool  s_was_cooling;  // para loguear el arranque de la fase una sola vez

void cooldown_tick(float dt_s)
{
    // 1. Leer estado bajo lock.
    app_state_lock();
    app_state_t *st = app_state_get();
    bool  cooling = st->cooling_active && (st->run_state == RUN_STATE_COMPLETED);
    float t_now   = st->last_sample.temperature;
    bool  t_fault = st->last_sample.fault;
    app_state_unlock();

    if (!cooling) {
        s_elapsed_s   = 0.0f;
        s_was_cooling = false;
        return;
    }

    if (!s_was_cooling) {
        s_was_cooling = true;
        ESP_LOGI(TAG, "enfriamiento post-proceso: ventilando hasta %d°C (tope %d min)",
                 (int)COOLDOWN_TEMP_C, (int)(COOLDOWN_DURATION_S / 60));
    }

    s_elapsed_s += dt_s;

    // 2. Cortar cuando la temperatura baja al objetivo (con sensor OK) O al
    //    cumplirse el tope de tiempo (salvaguarda si el ambiente no deja bajar
    //    a esa temperatura). Lo que ocurra primero.
    bool reached_temp = !t_fault && (t_now <= COOLDOWN_TEMP_C);
    bool timed_out    = (s_elapsed_s >= (float)COOLDOWN_DURATION_S);
    if (!reached_temp && !timed_out) return;

    app_state_lock();
    app_state_t *s = app_state_get();
    if (s->cooling_active && s->run_state == RUN_STATE_COMPLETED) {
        s->fan_command_on = false;
        s->cooling_active = false;
    }
    app_state_unlock();

    if (reached_temp)
        ESP_LOGI(TAG, "enfriamiento terminado: T=%.1f°C <= %d°C — fan OFF",
                 t_now, (int)COOLDOWN_TEMP_C);
    else
        ESP_LOGW(TAG, "enfriamiento: tope de %d min alcanzado (T=%.1f°C) — fan OFF",
                 (int)(COOLDOWN_DURATION_S / 60), t_now);

    s_elapsed_s   = 0.0f;
    s_was_cooling = false;
}
