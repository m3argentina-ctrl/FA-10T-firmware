#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Screen ids -------------------------------------------------------------
typedef enum {
    UI_SCREEN_SPLASH = 0,
    UI_SCREEN_INICIO,
    UI_SCREEN_PROG_MANUAL,
    UI_SCREEN_FUNC_MANUAL,
    UI_SCREEN_PROG_PROGRAMAS,   // lista de selección de slots PROG 1..6
    UI_SCREEN_PROG_STAGE,       // wizard: editar etapa 1/2/3 (se reconstruye)
    UI_SCREEN_PROG_RESUMEN,     // wizard: resumen + nombre + GRABAR/INICIAR
    UI_SCREEN_FUNC_PROGRAMAS,
    UI_SCREEN_TECNICA,
    UI_SCREEN_ALARMA,
    UI_SCREEN_COUNT,
} ui_screen_id_t;

// --- Colors (per spec) ------------------------------------------------------
#define UI_COL_PANEL_BG     lv_color_hex(0x111111)
#define UI_COL_LEFT_BG      lv_color_hex(0xFFFFFF)
#define UI_COL_ORANGE       lv_color_hex(0xE87A20)
#define UI_COL_GREEN        lv_color_hex(0x2E9E3B)
#define UI_COL_GREEN_DEEP   lv_color_hex(0x2E7D32)
#define UI_COL_BLUE         lv_color_hex(0x2196F3)
#define UI_COL_RED          lv_color_hex(0xD32F2F)
#define UI_COL_GREY_BTN     lv_color_hex(0x888888)
#define UI_COL_TEMP_RED     lv_color_hex(0xD03000)
#define UI_COL_TIME_BLUE    lv_color_hex(0x1565C0)
#define UI_COL_YELLOW       lv_color_hex(0xFDD835)
#define UI_COL_LABEL_GREY   lv_color_hex(0x888888)
#define UI_COL_CYAN         lv_color_hex(0x4FC3F7)
#define UI_COL_WHITE        lv_color_hex(0xFFFFFF)
#define UI_COL_BLACK        lv_color_hex(0x000000)

#define UI_LEFT_PANEL_W     120
#define UI_RIGHT_X          120

// --- Init / navigation ------------------------------------------------------
void ui_common_init(void);     // creates all screens; call once after lv_init
void ui_show_screen(ui_screen_id_t id);

// Returns the lv_obj_t* for the given screen id, or NULL if not built yet.
// Used by the wizard to clean+rebuild PROG_STAGE / PROG_RESUMEN dynamically.
lv_obj_t *ui_get_screen(ui_screen_id_t id);

// --- Reusable left-side navigation panel ------------------------------------
// Each screen that wants the nav panel calls ui_left_panel_attach(scr, active)
// during its build callback. The "active" id picks which row gets the green
// triangle indicator.
void ui_left_panel_attach(lv_obj_t *scr, ui_screen_id_t active);

// --- Helpers ----------------------------------------------------------------
lv_obj_t *ui_make_right_pane(lv_obj_t *scr);              // dark right panel
lv_obj_t *ui_make_box(lv_obj_t *parent, lv_color_t bg, lv_color_t border);

// Fonts (resolved at build time to whichever Montserrat sizes are enabled).
const lv_font_t *ui_font_xs(void);    // 10
const lv_font_t *ui_font_sm(void);    // 12
const lv_font_t *ui_font_md(void);    // 14
const lv_font_t *ui_font_lg(void);    // 18
const lv_font_t *ui_font_xl(void);    // 22
const lv_font_t *ui_font_xxl(void);   // 28
const lv_font_t *ui_font_huge(void);  // 46

// Montserrat Bold — generadas con lv_font_conv desde el .ttf oficial. Usadas
// en los valores de las cajitas de info de FUNC_MANUAL / FUNC_PROGRAMAS para
// dar peso visual a los números sobre fondos con borde colorido.
const lv_font_t *ui_font_xs_bold(void);   // 10, bold — captions de cajitas chicas
const lv_font_t *ui_font_sm_bold(void);   // 12, bold — captions de cajitas
const lv_font_t *ui_font_md_bold(void);   // 14, bold — valores de cajitas chicas
const lv_font_t *ui_font_lg_bold(void);   // 18, bold — valores de cajitas

// Each screen exposes a build() that constructs widgets into the given scr.
// ui_common_init() calls them all once. The PROG_STAGE / PROG_RESUMEN
// builders also get re-invoked by the wizard after lv_obj_clean() to refresh
// their contents — they must therefore be idempotent against any global state
// they depend on (the wizard buffer in prog_wizard_state()).
void screen_splash_build(lv_obj_t *scr);
void screen_inicio_build(lv_obj_t *scr);
void screen_prog_manual_build(lv_obj_t *scr);
void screen_func_manual_build(lv_obj_t *scr);
void screen_prog_select_build(lv_obj_t *scr);   // UI_SCREEN_PROG_PROGRAMAS
void screen_prog_stage_build(lv_obj_t *scr);
void screen_prog_resumen_build(lv_obj_t *scr);
void screen_func_programas_build(lv_obj_t *scr);
void screen_tecnica_build(lv_obj_t *scr);
void screen_alarma_build(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
