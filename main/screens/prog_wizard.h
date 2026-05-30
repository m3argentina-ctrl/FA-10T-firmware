#pragma once

// Estado compartido y navegación del wizard de PROGRAMAS.
//
// Flujo:
//   PROG_PROGRAMAS (select)  → tap PROG N → prog_wizard_enter(N-1)
//   PROG_STAGE (etapa 0/1/2) → SIGUIENTE/ATRÁS → prog_wizard_goto_stage(i)
//                              o, desde etapa 2 + SIGUIENTE → prog_wizard_show_resumen()
//   PROG_RESUMEN             → ATRÁS (vuelve a etapa 2) | GRABAR | INICIAR

#include <stdint.h>

#include "programa.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    programa_t recipe;          // buffer en RAM con lo que se está editando
    uint8_t    slot;            // 0..PROGRAMA_SLOTS-1
    uint8_t    current_stage;   // 0..PROG_STAGE_COUNT-1
} prog_wizard_state_t;

// Devuelve el puntero al estado interno. Llamar entre LVGL frames está OK.
prog_wizard_state_t *prog_wizard_state(void);

// Cargar el slot indicado desde NVS al buffer y navegar a la etapa 0.
// Si el slot está vacío en NVS, pre-llena con defaults razonables y nombre
// auto-generado "PROG <N>".
void prog_wizard_enter(uint8_t slot);

// Cambiar la etapa visible (0..2). Limpia y reconstruye la pantalla
// UI_SCREEN_PROG_STAGE con el contenido de la etapa pedida y la muestra.
void prog_wizard_goto_stage(uint8_t stage);

// Limpiar y reconstruir UI_SCREEN_PROG_RESUMEN con los valores actuales
// del buffer y mostrarla.
void prog_wizard_show_resumen(void);

// Persistir el buffer al slot y volver a UI_SCREEN_PROG_PROGRAMAS.
void prog_wizard_save_and_exit(void);

// Iniciar la sesión con el buffer actual y navegar a UI_SCREEN_FUNC_PROGRAMAS.
// Si grabar_primero=true, también persiste al slot antes de arrancar.
void prog_wizard_start_session(bool grabar_primero);

// Cargar un slot de NVS y arrancar la sesión directamente, sin pasar por el
// wizard de edición. Usado por la pantalla de selección cuando el usuario
// elige "INICIAR" sobre un programa ya grabado.
void prog_wizard_start_slot(uint8_t slot);

#ifdef __cplusplus
}
#endif
