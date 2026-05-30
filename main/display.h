#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Resolution after landscape rotation
#define LCD_H_RES 480
#define LCD_V_RES 320

// Initialise SPI, ST7796 panel, FT6336 touch and the LVGL display/input drivers.
// In SIMULATION_MODE the hardware path is skipped and LVGL is given stub
// buffers so screens can still be built and inspected.
esp_err_t display_init(void);

// LVGL is single-threaded; any UI manipulation from outside ui_task must take
// this lock. ui_task uses it around lv_timer_handler() too.
bool display_lvgl_lock(int timeout_ms);
void display_lvgl_unlock(void);

// Backlight brightness floor: nunca se atenúa por debajo de esto mientras está
// encendido, para que el panel siga siendo legible (y el operario pueda volver
// a subirlo). El slider de ÁREA TÉCNICA usa este mínimo como tope inferior.
#define DISPLAY_BL_PCT_MIN  10

// Fija el brillo del backlight (PWM por LEDC) en 0..100 %. Se clampa a
// [DISPLAY_BL_PCT_MIN, 100]. NO escribe NVS (para no castigar el flash al
// arrastrar el slider) — usar display_backlight_save() para persistir.
// No-op cuando se simula.
void display_set_backlight(uint8_t pct);

// Brillo actual (0..100 %). Devuelve el valor cargado de NVS al boot si todavía
// no se cambió. Válido también en SIMULATION_MODE.
uint8_t display_get_backlight(void);

// Persiste el brillo actual en NVS (namespace propio "fa10t_disp", independiente
// del blob de config versionado) para que sobreviva al reboot. No-op al simular.
void display_backlight_save(void);

// Auto-atenuado por inactividad (lo gobierna ui_task con el tiempo idle de
// LVGL). dim=true baja el backlight a DISPLAY_BL_DIM_PCT SIN tocar el brillo
// configurado ni NVS; dim=false restaura el brillo del operario. Idempotente:
// llamarla repetidamente con el mismo estado no reescribe el PWM. No-op al
// simular. El primer toque resetea el idle de LVGL → ui_task lo des-atenúa.
void display_backlight_set_dimmed(bool dimmed);

// Enable / disable the on-board audio power amplifier (NS4150) via the TCA9554
// I/O expander's EXIO7 line. Must be set to true before any audio playback;
// keep false otherwise to avoid hiss. No-op when simulating.
void display_set_pa_amp(bool on);

#ifdef __cplusplus
}
#endif
