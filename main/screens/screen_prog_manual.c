#include "ui_common.h"
#include "modo_manual.h"

#include <stdio.h>
#include <stdint.h>

// Manual-mode configuration is kept in module-static state until the user
// presses INICIAR, at which point we hand it to modo_manual_start().
static float    s_temp_c   = 60.0f;
static uint32_t s_time_s   = 60 * 60;   // 01:00

static lv_obj_t *s_temp_lbl;
static lv_obj_t *s_time_lbl;

static lv_timer_t *s_hold_timer;
static int8_t      s_hold_dir;          // -1 / +1
static uint8_t     s_hold_kind;         // 0=temp, 1=time
static uint32_t    s_hold_count;        // pulses since press began

static void refresh_labels(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", s_temp_c);
    lv_label_set_text(s_temp_lbl, buf);
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(s_time_s / 3600),
             (unsigned long)((s_time_s / 60) % 60));
    lv_label_set_text(s_time_lbl, buf);
}

static uint32_t time_step_for_pulse(uint32_t pulse)
{
    if (pulse <  5) return 60;          // 1 min
    if (pulse < 15) return 5 * 60;      // 5 min
    if (pulse < 25) return 10 * 60;     // 10 min
    return 30 * 60;                     // 30 min
}

static void apply_step(int8_t dir, uint8_t kind, uint32_t pulse)
{
    if (kind == 0) {
        s_temp_c += 0.1f * dir;
        if (s_temp_c < 20.0f) s_temp_c = 20.0f;
        if (s_temp_c > 80.0f) s_temp_c = 80.0f;
    } else {
        uint32_t step = time_step_for_pulse(pulse);
        if (dir > 0) s_time_s += step;
        else if (s_time_s > step) s_time_s -= step;
        else s_time_s = 60;             // clamp ≥ 1 min

        if (s_time_s > 99u * 3600 + 59u * 60) s_time_s = 99u * 3600 + 59u * 60;
        if (s_time_s < 60)                    s_time_s = 60;
    }
    refresh_labels();
}

static void hold_timer_cb(lv_timer_t *t)
{
    (void)t;
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
}

static void spin_press_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    s_hold_dir   = (code & 0x01) ? +1 : -1;
    s_hold_kind  = (code >> 1) & 0x01;
    s_hold_count = 0;
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
    if (!s_hold_timer) s_hold_timer = lv_timer_create(hold_timer_cb, 120, NULL);
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
    lv_obj_add_event_cb(b, spin_press_cb,   LV_EVENT_PRESSED,         (void *)code);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_RELEASED,        NULL);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_PRESS_LOST,      NULL);
    return b;
}

static void iniciar_cb(lv_event_t *e)
{
    (void)e;
    if (modo_manual_start(s_temp_c, s_time_s) == ESP_OK) {
        ui_show_screen(UI_SCREEN_FUNC_MANUAL);
    }
}

void screen_prog_manual_build(lv_obj_t *scr)
{
    // Si el usuario navegó con el dedo todavía apoyado sobre un spin button,
    // los callbacks RELEASED/PRESS_LOST pueden no dispararse y el timer de
    // auto-repeat quedaría vivo apuntando a labels ya destruidos. Lo matamos
    // al reconstruir la pantalla (mismo patrón que screen_prog_stage.c).
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }

    ui_left_panel_attach(scr, UI_SCREEN_PROG_MANUAL);
    lv_obj_t *p = ui_make_right_pane(scr);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "PROGRAMACION \"MODO MANUAL\"");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // --- Temperature block ---
    lv_obj_t *temp_lbl = lv_label_create(p);
    lv_label_set_text(temp_lbl, "TEMPERATURA °C");
    lv_obj_set_style_text_font(temp_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(temp_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_MID, 0, 44);

    // Code bit 0: dir (+1 if set), bit 1: kind (1=time)
    lv_obj_t *t_minus = make_spin_btn(p, UI_COL_TIME_BLUE, "-", 0b00);
    lv_obj_align(t_minus, LV_ALIGN_TOP_MID, -130, 72);

    s_temp_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_temp_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_temp_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *t_plus = make_spin_btn(p, UI_COL_RED, "+", 0b01);
    lv_obj_align(t_plus, LV_ALIGN_TOP_MID, 130, 72);

    // --- Time block ---
    lv_obj_t *time_lbl = lv_label_create(p);
    lv_label_set_text(time_lbl, "TIEMPO HH:MM");
    lv_obj_set_style_text_font(time_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(time_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(time_lbl, LV_ALIGN_TOP_MID, 0, 142);

    lv_obj_t *m_minus = make_spin_btn(p, UI_COL_GREEN, "-", 0b10);
    lv_obj_align(m_minus, LV_ALIGN_TOP_MID, -130, 170);

    s_time_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_time_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 166);

    lv_obj_t *m_plus = make_spin_btn(p, UI_COL_GREEN, "+", 0b11);
    lv_obj_align(m_plus, LV_ALIGN_TOP_MID, 130, 170);

    refresh_labels();

    // --- INICIAR ---
    lv_obj_t *go = lv_btn_create(p);
    lv_obj_set_size(go, 260, 40);
    lv_obj_align(go, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(go, UI_COL_ORANGE, 0);
    lv_obj_add_event_cb(go, iniciar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go);
    lv_label_set_text(gl, "INICIAR");
    lv_obj_set_style_text_font(gl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(gl, UI_COL_WHITE, 0);
    lv_obj_center(gl);
}
