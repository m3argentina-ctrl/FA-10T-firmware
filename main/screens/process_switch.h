#pragma once

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Guard de cambio de tipo de proceso.
//
// Reemplaza a ui_show_screen(target) cuando el usuario navega a MANUAL o
// PROGRAMAS desde el menú izquierdo o desde los botones de INICIO. Si no hay
// ninguna sesión viva (RUNNING o PAUSED), navega directo. Si hay una sesión
// en curso —del mismo tipo o del otro— levanta un modal a pantalla completa:
//
//   CONTINUAR (verde) → vuelve a la pantalla FUNC de la sesión viva (no toca
//                       el proceso). También resuelve el caso de "¿cómo vuelvo
//                       a la vista del proceso en marcha?".
//   DETENER  (rojo)   → detiene la sesión viva (modo_manual_stop /
//                       programa_stop) y recién ahí navega a `target`.
//
// `target` sólo debe ser UI_SCREEN_PROG_MANUAL o UI_SCREEN_PROG_PROGRAMAS.
void process_switch_request(ui_screen_id_t target);

#ifdef __cplusplus
}
#endif
