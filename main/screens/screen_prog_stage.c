// Pantalla UI_SCREEN_PROG_STAGE: editor de UNA etapa del wizard de programas.
// Layout grande estilo MODO MANUAL (botones 44×44, labels font_huge).
//
// El builder se llama también desde prog_wizard_goto_stage() tras lv_obj_clean()
// para refrescar el contenido cuando cambia la etapa activa, así que TODO el
// estado UI propio de esta pantalla (labels, hold timer) es file-static y se
// re-inicializa en cada build.

#include "ui_common.h"
#include "prog_wizard.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>

// Auto-repeat de los spin buttons. HOLD_DELAY_MS es la espera ANTES del primer
// auto-repeat: con un valor alto, un toque corto normal (~150-250 ms) ejecuta un
// solo paso (no dos), así se puede afinar la temperatura de a 0,1 sin saltos.
// Al mantener apretado, tras esa espera el repeat pasa a HOLD_REPEAT_MS (rápido).
#define HOLD_DELAY_MS   450
#define HOLD_REPEAT_MS  90

static lv_obj_t   *s_temp_lbl;
static lv_obj_t   *s_time_lbl;
static lv_obj_t   *s_title;

static lv_timer_t *s_hold_timer;
static int8_t      s_hold_dir;
static uint8_t     s_hold_kind;     // 0=temp, 1=time
static uint32_t    s_hold_count;

static void refresh_labels(void)
{
    prog_wizard_state_t *st = prog_wizard_state();
    uint8_t i = st->current_stage;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", st->recipe.etapa_sp[i]);
    lv_label_set_text(s_temp_lbl, buf);
    uint32_t s = st->recipe.etapa_duration_s[i];
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
    lv_label_set_text(s_time_lbl, buf);
}

static uint32_t time_step_for_pulse(uint32_t pulse)
{
    if (pulse <  5) return 60;          // 1 min
    if (pulse < 15) return 5 * 60;      // 5 min
    if (pulse < 25) return 10 * 60;     // 10 min
    return 30 * 60;                     // 30 min
}

// Paso de temperatura con aceleración (igual criterio que el de tiempo): un
// toque corto (pulse 0) sube 0,1 °C para afinar; manteniendo apretado acelera a
// 1/2/5 °C para recorrer todo el rango 20-80 °C en un par de segundos.
static float temp_step_for_pulse(uint32_t pulse)
{
    if (pulse <  5) return 0.1f;        // afinado: toque corto = 0,1 °C
    if (pulse < 15) return 1.0f;
    if (pulse < 25) return 2.0f;
    return 5.0f;                        // rápido
}

static void apply_step(int8_t dir, uint8_t kind, uint32_t pulse)
{
    prog_wizard_state_t *st = prog_wizard_state();
    uint8_t i = st->current_stage;
    if (kind == 0) {
        float *sp = &st->recipe.etapa_sp[i];
        float step = temp_step_for_pulse(pulse);
        *sp += step * dir;
        // En modo rápido caemos en grados enteros; el afinado mantiene 0,1 °C.
        if (step >= 1.0f) *sp = roundf(*sp);
        else              *sp = roundf(*sp * 10.0f) / 10.0f;
        if (*sp < 20.0f) *sp = 20.0f;
        if (*sp > 80.0f) *sp = 80.0f;
    } else {
        uint32_t step = time_step_for_pulse(pulse);
        uint32_t *d   = &st->recipe.etapa_duration_s[i];
        if (dir > 0)         *d += step;
        else if (*d > step)  *d -= step;
        else                 *d  = 60;
        if (*d > 99u * 3600 + 59u * 60) *d = 99u * 3600 + 59u * 60;
        if (*d < 60)                    *d = 60;
    }
    refresh_labels();
}

static void hold_timer_cb(lv_timer_t *t)
{
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
    lv_timer_set_period(t, HOLD_REPEAT_MS);   // tras el 1er disparo, acelera
}

static void spin_press_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    s_hold_dir   = (code & 0x01) ? +1 : -1;
    s_hold_kind  = (code >> 1) & 0x01;
    s_hold_count = 0;
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
    if (!s_hold_timer) s_hold_timer = lv_timer_create(hold_timer_cb, HOLD_DELAY_MS, NULL);
}

static void spin_release_cb(lv_event_t *e)
{
    (void)e;
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }
}

static lv_obj_t *make_spin_btn(lv_obj_t *parent, lv_color_t col,
                               const char *txt, intptr_t code)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 44, 44);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, ui_font_xxl(), 0);
    lv_obj_set_style_text_color(l, UI_COL_WHITE, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, spin_press_cb,   LV_EVENT_PRESSED,    (void *)code);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_PRESS_LOST, NULL);
    return b;
}

static void atras_cb(lv_event_t *e)
{
    (void)e;
    prog_wizard_state_t *st = prog_wizard_state();
    if (st->current_stage == 0) {
        ui_show_screen(UI_SCREEN_PROG_PROGRAMAS);
    } else {
        prog_wizard_goto_stage(st->current_stage - 1);
    }
}

static void siguiente_cb(lv_event_t *e)
{
    (void)e;
    prog_wizard_state_t *st = prog_wizard_state();
    if (st->current_stage + 1 >= PROG_STAGE_COUNT) {
        prog_wizard_show_resumen();
    } else {
        prog_wizard_goto_stage(st->current_stage + 1);
    }
}

void screen_prog_stage_build(lv_obj_t *scr)
{
    // Cualquier hold_timer pendiente entre builds (cambio de etapa con dedo
    // todavía apoyado, edge case) debe limpiarse.
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }

    ui_left_panel_attach(scr, UI_SCREEN_PROG_PROGRAMAS);
    lv_obj_t *p = ui_make_right_pane(scr);

    prog_wizard_state_t *st = prog_wizard_state();

    char title_buf[40];
    snprintf(title_buf, sizeof(title_buf), "%s - ETAPA %u/%u",
             st->recipe.nombre,
             (unsigned)(st->current_stage + 1), (unsigned)PROG_STAGE_COUNT);
    s_title = lv_label_create(p);
    lv_label_set_text(s_title, title_buf);
    lv_obj_set_style_text_font(s_title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_title, UI_COL_GREEN, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 6);

    // --- TEMPERATURA ---
    lv_obj_t *temp_cap = lv_label_create(p);
    lv_label_set_text(temp_cap, "TEMPERATURA \xC2\xB0""C");
    lv_obj_set_style_text_font(temp_cap, ui_font_md(), 0);
    lv_obj_set_style_text_color(temp_cap, UI_COL_LABEL_GREY, 0);
    lv_obj_align(temp_cap, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *t_minus = make_spin_btn(p, UI_COL_TIME_BLUE, "-", 0b00);
    lv_obj_align(t_minus, LV_ALIGN_TOP_MID, -130, 66);
    s_temp_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_temp_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_temp_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *t_plus  = make_spin_btn(p, UI_COL_RED, "+", 0b01);
    lv_obj_align(t_plus, LV_ALIGN_TOP_MID, 130, 66);

    // --- TIEMPO ---
    lv_obj_t *time_cap = lv_label_create(p);
    lv_label_set_text(time_cap, "TIEMPO HH:MM");
    lv_obj_set_style_text_font(time_cap, ui_font_md(), 0);
    lv_obj_set_style_text_color(time_cap, UI_COL_LABEL_GREY, 0);
    lv_obj_align(time_cap, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *m_minus = make_spin_btn(p, UI_COL_GREEN, "-", 0b10);
    lv_obj_align(m_minus, LV_ALIGN_TOP_MID, -130, 158);
    s_time_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_time_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_t *m_plus  = make_spin_btn(p, UI_COL_GREEN, "+", 0b11);
    lv_obj_align(m_plus, LV_ALIGN_TOP_MID, 130, 158);

    refresh_labels();

    // --- ATRÁS / SIGUIENTE ---
    lv_obj_t *back = lv_btn_create(p);
    lv_obj_set_size(back, 120, 40);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -10);
    lv_obj_set_style_bg_color(back, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(back, atras_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  ATRAS");
    lv_obj_set_style_text_font(bl, ui_font_md(), 0);
    lv_obj_set_style_text_color(bl, UI_COL_WHITE, 0);
    lv_obj_center(bl);

    const bool last = (st->current_stage + 1 >= PROG_STAGE_COUNT);
    lv_obj_t *next = lv_btn_create(p);
    lv_obj_set_size(next, 160, 40);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -8, -10);
    lv_obj_set_style_bg_color(next, last ? UI_COL_ORANGE : UI_COL_GREEN, 0);
    lv_obj_add_event_cb(next, siguiente_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(next);
    lv_label_set_text(nl, last ? ("REVISAR  " LV_SYMBOL_RIGHT)
                               : ("SIGUIENTE  " LV_SYMBOL_RIGHT));
    lv_obj_set_style_text_font(nl, ui_font_md(), 0);
    lv_obj_set_style_text_color(nl, UI_COL_WHITE, 0);
    lv_obj_center(nl);
}
