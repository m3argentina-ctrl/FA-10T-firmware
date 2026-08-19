#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Control ON/OFF del extractor (salida AUX) por humedad de cámara, con
// histéresis + anti-cycling + fail-safe. Ver parámetros en app_config.h.
//
// Llamar una vez por ciclo del control_task. Devuelve el duty para SSR_CH_AUX
// (0.0 = apagado, 1.0 = encendido).
//   rh             : humedad relativa actual de cámara (%RH)
//   rh_fault       : true si el sensor de humedad está en falla
//   session_active : true si hay una sesión de secado corriendo
//   dt             : delta de tiempo desde la última llamada (s)
float extractor_tick(float rh, bool rh_fault, bool session_active, float dt);

// Estado actual (para UI/telemetría).
bool extractor_is_on(void);

#ifdef __cplusplus
}
#endif
