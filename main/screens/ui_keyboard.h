#pragma once

// Modal de teclado on-screen reutilizable. Single-instance: solo una invocación
// activa a la vez. Vive en lv_layer_top() para sobrevivir cambios de pantalla
// y aparecer por encima de cualquier screen.

#include "lvgl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_KB_MODE_TEXT     = 0,   // A-Z, 0-9, símbolos básicos
    UI_KB_MODE_NUMERIC  = 1,   // sólo 0-9 (+ password mask). Ver nota de rango.
} ui_keyboard_mode_t;

// CONTRATO DE VALIDACIÓN: este modal sólo limita la LONGITUD (max_len). NO
// valida rango numérico ni formato. En NUMERIC sólo garantiza dígitos 0-9.
// El done_cb es responsable de validar/clampear el valor (ej. setpoint dentro
// del envelope, PIN de N dígitos). Hoy NUMERIC se usa únicamente para PIN en
// screen_tecnica.c; los setpoints se editan con spin buttons (clamp en sitio),
// no con este teclado. Si en el futuro se edita un setpoint vía teclado, el
// callback DEBE rechazar/clampear fuera de OPERATING_TEMP_MIN/MAX_C.

// Llamado cuando el usuario toca OK. NO se llama si el usuario toca CANCELAR.
typedef void (*ui_keyboard_done_cb_t)(const char *new_text, void *user_data);

// Abrir el modal. Si ya está abierto otro keyboard, ignora la llamada.
// - initial:   texto inicial mostrado en el textarea (puede ser NULL → "").
// - max_len:   máxima cantidad de chars permitidos (incluyendo '\0' implícito).
// - title:     título del modal (ej. "EDITAR NOMBRE").
// - mode:      TEXT o NUMERIC.
// - done_cb:   callback cuando el usuario toca OK.
// - user_data: pasado tal cual al done_cb.
void ui_keyboard_open(const char           *initial,
                       size_t                max_len,
                       const char           *title,
                       ui_keyboard_mode_t    mode,
                       ui_keyboard_done_cb_t done_cb,
                       void                 *user_data);

#ifdef __cplusplus
}
#endif
