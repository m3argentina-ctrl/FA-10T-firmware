#include "extractor.h"
#include "app_config.h"

// Estado del extractor. Zero-init: arranca apagado.
static bool  s_on;
static float s_state_timer_s;   // tiempo en el estado actual (anti-cycling)

float extractor_tick(float rh, bool rh_fault, bool session_active, float dt)
{
    s_state_timer_s += dt;

    // Fuera de proceso: extractor apagado (y resetea el timer para que al
    // arrancar la próxima sesión pueda conmutar sin esperar el anti-cycling).
    if (!session_active) {
        if (s_on) { s_on = false; s_state_timer_s = 0.0f; }
        return 0.0f;
    }

    // Fail-safe: si el sensor de humedad falla, EXTRAER (mejor sacar aire de más
    // que dejar acumular vapor y arruinar el producto).
    if (rh_fault) {
        if (!s_on) { s_on = true; s_state_timer_s = 0.0f; }
        return 1.0f;
    }

    // Histéresis con tiempo mínimo en cada estado (anti-cycling del motor).
    if (s_on) {
        if (rh <= EXTRACTOR_RH_OFF_PCT && s_state_timer_s >= EXTRACTOR_MIN_ON_S) {
            s_on = false;
            s_state_timer_s = 0.0f;
        }
    } else {
        if (rh >= EXTRACTOR_RH_ON_PCT && s_state_timer_s >= EXTRACTOR_MIN_OFF_S) {
            s_on = true;
            s_state_timer_s = 0.0f;
        }
    }

    return s_on ? 1.0f : 0.0f;
}

bool extractor_is_on(void)
{
    return s_on;
}
