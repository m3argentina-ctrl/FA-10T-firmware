#pragma once

// Declaraciones de los assets gráficos embebidos.
// Los arrays .c están en main/screens/img/ y se generan con
// tools/png_to_lvgl.py desde los PNG en assets/.

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_IMG_DECLARE(isologo_64);   // 64×64, RGB565 — panel izq + INICIO right pane
LV_IMG_DECLARE(logo_300);     // 300×196, RGB565 — SPLASH
// 160×105, RGB565 — INICIO. Tiene fondo BLANCO (el PNG no trae alpha), así que
// va dentro de una tarjeta blanca; sobre el panel oscuro quedaría un recuadro.
LV_IMG_DECLARE(logo_inicio);

#ifdef __cplusplus
}
#endif
