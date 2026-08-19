#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Asistente modal de calibración de la sonda de temperatura por DOS PUNTOS (agua con hielo = 0 C
// y agua hirviendo = punto de ebullición según altitud, editable). Captura la
// temperatura CRUDA (pre-calibración) en cada punto, calcula la recta
//   T_real = cal_gain * T_medido + cal_offset
// y la guarda en NVS (fa10t_config_t). Lo dispara el botón CALIBRAR de la
// pantalla técnica. Idempotente: no-op si ya está abierto.
void calib_prompt_show(void);

// True mientras el modal de calibración esté visible.
bool calib_prompt_active(void);

#ifdef __cplusplus
}
#endif
