#pragma once

// Modal "¿RECUPERAR SESIÓN?" para power-fail recovery. Vive en lv_layer_top()
// (igual que ui_keyboard), así aparece por encima de cualquier pantalla — se
// muestra al arranque cuando recovery_has_session() detecta una sesión que
// quedó corriendo en el último corte de energía.
//
// Comportamiento: cuenta regresiva (RECOVERY_COUNTDOWN_S). Si nadie toca,
// REANUDA automáticamente. El operario puede RECUPERAR ya o DESCARTAR.

#include "recovery.h"

#ifdef __cplusplus
extern "C" {
#endif

// Muestra el modal. El snapshot se copia internamente (no hace falta que el
// caller lo mantenga vivo). No hace nada si snap es NULL/!valid o si ya hay
// un modal activo.
void recovery_prompt_show(const recovery_snapshot_t *snap);

// True mientras el modal está visible. El splash lo consulta para no pisar la
// navegación que decide el modal al cerrarse.
bool recovery_prompt_active(void);

#ifdef __cplusplus
}
#endif
