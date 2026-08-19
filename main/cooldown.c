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
    bool cooling = st->cooling_active && (st->run_state == RUN_STATE_COMPLETED);
    app_state_unlock();

    if (!cooling) {
        s_elapsed_s   = 0.0f;
        s_was_cooling = false;
        return;
    }

    if (!s_was_cooling) {
        s_was_cooling = true;
        ESP_LOGI(TAG, "enfriamiento post-proceso: ventilando %d min",
                 (int)(COOLDOWN_DURATION_S / 60));
    }

    s_elapsed_s += dt_s;
    if (s_elapsed_s < (float)COOLDOWN_DURATION_S) return;

    // 2. Cumplido el tiempo: cortar el fan (revalidando el estado bajo lock).
    app_state_lock();
    app_state_t *s = app_state_get();
    if (s->cooling_active && s->run_state == RUN_STATE_COMPLETED) {
        s->fan_command_on = false;
        s->cooling_active = false;
    }
    app_state_unlock();

    ESP_LOGI(TAG, "enfriamiento post-proceso terminado (%d min) — fan OFF",
             (int)(COOLDOWN_DURATION_S / 60));
    s_elapsed_s   = 0.0f;
    s_was_cooling = false;
}
