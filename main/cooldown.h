#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Enfriamiento post-proceso.
//
// Al COMPLETAR una sesión (por tiempo o por humedad), el calefactor se apaga
// pero las turbinas siguen ventilando (fan_command_on queda true y
// cooling_active=true, seteados por el punto de completado). Este módulo
// supervisa esa fase y corta el fan al cumplirse COOLDOWN_DURATION_S de
// ventilación (solo por tiempo — baja todo lo que el ambiente permita).
//
// Si el operario detiene la sesión o arranca otra, los stop()/start() ya
// limpian fan_command_on / cooling_active y esto queda no-op.
//
// Se llama desde control_task cada ciclo, después de los ticks de sesión.
void cooldown_tick(float dt_s);

#ifdef __cplusplus
}
#endif
