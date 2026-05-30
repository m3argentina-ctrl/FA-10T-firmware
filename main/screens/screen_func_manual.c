#include "ui_common.h"
#include "app_state.h"
#include "modo_manual.h"
#include "safety.h"

#include <stdio.h>

// Widgets actualizados desde update_cb cada 500 ms.
static lv_obj_t *s_owner_scr;
static lv_obj_t *s_state_bar, *s_state_lbl;
static lv_obj_t *s_sp_lbl, *s_tprog_lbl, *s_tmin_lbl, *s_tmax_lbl;
static lv_obj_t *s_res_box, *s_res_lbl;
static lv_obj_t *s_fan_box, *s_fan_lbl;
static lv_obj_t *s_big_temp, *s_temp_sp_lbl;
static lv_obj_t *s_big_time, *s_time_total_lbl;
static lv_obj_t *s_temp_bar, *s_time_bar;
static lv_obj_t *s_pausar_btn;

static void format_hhmmss(uint32_t s, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

static void set_onoff(lv_obj_t *box, lv_obj_t *lbl, bool on)
{
    lv_label_set_text(lbl, on ? "ON" : "OFF");
    lv_obj_set_style_bg_color(box, on ? UI_COL_GREEN : UI_COL_RED, 0);
}

static void update_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != s_owner_scr) return;

    app_state_lock();
    const app_state_t s = *app_state_get();
    app_state_unlock();

    char buf[40];

    // Cajitas
    snprintf(buf, sizeof(buf), "%.1f", s.effective_setpoint); lv_label_set_text(s_sp_lbl, buf);
    format_hhmmss(s.session_total_s, buf, sizeof(buf));       lv_label_set_text(s_tprog_lbl, buf);
    snprintf(buf, sizeof(buf), "%.1f", s.t_min_sesion >  998.0f ? 0.0f : s.t_min_sesion);
    lv_label_set_text(s_tmin_lbl, buf);
    snprintf(buf, sizeof(buf), "%.1f", s.t_max_sesion < -998.0f ? 0.0f : s.t_max_sesion);
    lv_label_set_text(s_tmax_lbl, buf);

    set_onoff(s_res_box, s_res_lbl, s.ssr_drv_duty > 0.05f);
    set_onoff(s_fan_box, s_fan_lbl, s.ssr_fan_duty > 0.05f);

    // TEMP grande
    snprintf(buf, sizeof(buf), "%.1f", s.last_sample.temperature);
    lv_label_set_text(s_big_temp, buf);
    snprintf(buf, sizeof(buf), "/ %.0f \xC2\xB0""C", s.effective_setpoint);
    lv_label_set_text(s_temp_sp_lbl, buf);

    // TIEMPO grande / CALENTANDO
    if (!s.warmup_done && s.run_state == RUN_STATE_RUNNING) {
        lv_label_set_text(s_big_time, "CALENTANDO");
        lv_obj_set_style_text_color(s_big_time, UI_COL_YELLOW, 0);
        lv_label_set_text(s_time_total_lbl, "esperando setpoint");
    } else {
        format_hhmmss(s.session_remaining_s, buf, sizeof(buf));
        lv_label_set_text(s_big_time, buf);
        lv_obj_set_style_text_color(s_big_time, UI_COL_TIME_BLUE, 0);
        char tot[24];
        format_hhmmss(s.session_total_s, tot, sizeof(tot));
        snprintf(buf, sizeof(buf), "/ %s total", tot);
        lv_label_set_text(s_time_total_lbl, buf);
    }

    // Barras
    int32_t t_pct = (int32_t)s.last_sample.temperature;
    if (t_pct < 0)   t_pct = 0;
    if (t_pct > 100) t_pct = 100;
    lv_bar_set_value(s_temp_bar, t_pct, LV_ANIM_OFF);

    int32_t pct = 0;
    if (s.session_total_s > 0 && s.warmup_done) {
        // uint64 en el producto: session_elapsed_s es uint32 y *100 podría
        // desbordar (no en sesiones reales <100 h, pero el cast es gratis).
        pct = (int32_t)(((uint64_t)s.session_elapsed_s * 100) / s.session_total_s);
    }
    lv_bar_set_value(s_time_bar, pct, LV_ANIM_OFF);

    // Estado
    if (s.safety_faults & SAFETY_TRIP_MASK) { ui_show_screen(UI_SCREEN_ALARMA); return; }
    if (s.run_state == RUN_STATE_PAUSED) {
        lv_label_set_text(s_state_lbl, "PAUSADO");
        lv_obj_set_style_bg_color(s_state_bar, UI_COL_YELLOW, 0);
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_GREY_BTN, 0);
    } else if (s.run_state == RUN_STATE_COMPLETED) {
        lv_label_set_text(s_state_lbl, "COMPLETADO");
        lv_obj_set_style_bg_color(s_state_bar, UI_COL_GREEN, 0);
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_GREY_BTN, 0);
    } else if (!s.warmup_done) {
        lv_label_set_text(s_state_lbl, "CALENTANDO");
        lv_obj_set_style_bg_color(s_state_bar, UI_COL_ORANGE, 0);
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_ORANGE, 0);
    } else {
        lv_label_set_text(s_state_lbl, "FUNCIONANDO");
        lv_obj_set_style_bg_color(s_state_bar, UI_COL_GREEN, 0);
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_ORANGE, 0);
    }
}

// --- Botones ---
static void pausar_cb(lv_event_t *e)    { (void)e; modo_manual_pause();  }
static void reiniciar_cb(lv_event_t *e) { (void)e; modo_manual_resume(); }
static void detener_cb(lv_event_t *e)
{
    (void)e;
    modo_manual_stop();
    ui_show_screen(UI_SCREEN_PROG_MANUAL);
}

// --- Cajón con leyenda arriba y valor abajo. h=40 evita overlap caption↔value
// con font_md en el valor. ---
static lv_obj_t *make_info_box(lv_obj_t *parent, const char *caption,
                               int x, int y, int w,
                               lv_color_t border, lv_color_t value_col,
                               lv_obj_t **out_value)
{
    lv_obj_t *b = ui_make_box(parent, UI_COL_PANEL_BG, border);
    lv_obj_set_size(b, w, 40);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_pad_all(b, 2, 0);

    lv_obj_t *cap = lv_label_create(b);
    lv_label_set_text(cap, caption);
    // Montserrat Bold 12 px (sm_bold) en lugar de regular 10 px (xs): los
    // captions de 10 px se veían borrosos por aliasing sobre cajas chicas
    // con borde colorido. Bold 12 da más resolución vertical + peso visual.
    lv_obj_set_style_text_font(cap, ui_font_sm_bold(), 0);
    lv_obj_set_style_text_color(cap, UI_COL_WHITE, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *val = lv_label_create(b);
    // Bold 18 px (lg_bold) en lugar de regular 14 (md): peso visual fuerte
    // sobre cajas con borde, mucho más legible para los números/ON|OFF.
    lv_obj_set_style_text_font(val, ui_font_lg_bold(), 0);
    lv_obj_set_style_text_color(val, value_col, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(val, "-");
    *out_value = val;
    return b;
}

void screen_func_manual_build(lv_obj_t *scr)
{
    s_owner_scr = scr;
    ui_left_panel_attach(scr, UI_SCREEN_PROG_MANUAL);
    lv_obj_t *p = ui_make_right_pane(scr);

    // y=0..20: barra de estado
    s_state_bar = lv_obj_create(p);
    lv_obj_remove_style_all(s_state_bar);
    lv_obj_set_size(s_state_bar, LV_HOR_RES - UI_RIGHT_X, 20);
    lv_obj_align(s_state_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_state_bar, UI_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(s_state_bar, LV_OPA_COVER, 0);
    s_state_lbl = lv_label_create(s_state_bar);
    lv_obj_set_style_text_font(s_state_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_state_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_state_lbl);
    lv_label_set_text(s_state_lbl, "MANUAL");

    // y=24..64: row de 4 cajitas SET POINT | T PROGRAMADO | RESISTENCIA | TURBINAS
    const int BW = 86, GAP = 2;
    int x = 4;
    make_info_box(p, "SET POINT",  x, 24, BW, UI_COL_ORANGE, UI_COL_ORANGE, &s_sp_lbl);    x += BW + GAP;
    make_info_box(p, "T PROGRAM.", x, 24, BW, UI_COL_TIME_BLUE, UI_COL_TIME_BLUE, &s_tprog_lbl); x += BW + GAP;
    s_res_box = make_info_box(p, "RESISTENCIA", x, 24, BW, UI_COL_GREEN, UI_COL_WHITE, &s_res_lbl); x += BW + GAP;
    s_fan_box = make_info_box(p, "TURBINAS",    x, 24, BW, UI_COL_GREEN, UI_COL_WHITE, &s_fan_lbl);

    // y=70..118: TEMPERATURA grande + label SP
    s_big_temp = lv_label_create(p);
    lv_obj_set_style_text_font(s_big_temp, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_big_temp, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_big_temp, LV_ALIGN_TOP_LEFT, 14, 68);
    lv_label_set_text(s_big_temp, "--.-");

    s_temp_sp_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_temp_sp_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_temp_sp_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(s_temp_sp_lbl, LV_ALIGN_TOP_RIGHT, -12, 92);
    lv_label_set_text(s_temp_sp_lbl, "/ -- \xC2\xB0""C");

    // y=120..132: barra temp
    s_temp_bar = lv_bar_create(p);
    lv_obj_set_size(s_temp_bar, 340, 12);
    lv_obj_align(s_temp_bar, LV_ALIGN_TOP_LEFT, 8, 120);
    lv_bar_set_range(s_temp_bar, 0, 100);
    lv_obj_set_style_bg_color(s_temp_bar, UI_COL_GREY_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_temp_bar, UI_COL_TEMP_RED, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_temp_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_temp_bar, 5, LV_PART_INDICATOR);

    // y=138..186: TIEMPO grande + label total
    s_big_time = lv_label_create(p);
    lv_obj_set_style_text_font(s_big_time, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_big_time, UI_COL_TIME_BLUE, 0);
    lv_obj_align(s_big_time, LV_ALIGN_TOP_LEFT, 14, 136);
    lv_label_set_text(s_big_time, "00:00:00");

    s_time_total_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_time_total_lbl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_time_total_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(s_time_total_lbl, LV_ALIGN_TOP_RIGHT, -12, 162);
    lv_label_set_text(s_time_total_lbl, "");

    // y=190..202: barra tiempo
    s_time_bar = lv_bar_create(p);
    lv_obj_set_size(s_time_bar, 340, 12);
    lv_obj_align(s_time_bar, LV_ALIGN_TOP_LEFT, 8, 188);
    lv_bar_set_range(s_time_bar, 0, 100);
    lv_obj_set_style_bg_color(s_time_bar, UI_COL_GREY_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_time_bar, UI_COL_TIME_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_time_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_time_bar, 5, LV_PART_INDICATOR);

    // y=208..248: row inferior T MIN | T MAX (centradas)
    const int B2_W = 110, B2_GAP = 12;
    int x2 = (LV_HOR_RES - UI_RIGHT_X - (2 * B2_W + B2_GAP)) / 2;
    make_info_box(p, "T MIN SESION", x2,               208, B2_W, UI_COL_CYAN,   UI_COL_CYAN,   &s_tmin_lbl);
    make_info_box(p, "T MAX SESION", x2 + B2_W + B2_GAP, 208, B2_W, UI_COL_YELLOW, UI_COL_YELLOW, &s_tmax_lbl);

    // Bottom: 3 botones
    s_pausar_btn = lv_btn_create(p);
    lv_obj_set_size(s_pausar_btn, 110, 38);
    lv_obj_align(s_pausar_btn, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_ORANGE, 0);
    lv_obj_add_event_cb(s_pausar_btn, pausar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(s_pausar_btn);
    lv_label_set_text(pl, "PAUSAR");
    lv_obj_set_style_text_font(pl, ui_font_md(), 0);
    lv_obj_set_style_text_color(pl, UI_COL_WHITE, 0);
    lv_obj_center(pl);

    lv_obj_t *rb = lv_btn_create(p);
    lv_obj_set_size(rb, 110, 38);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(rb, UI_COL_BLUE, 0);
    lv_obj_add_event_cb(rb, reiniciar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, "REINICIAR");
    lv_obj_set_style_text_font(rl, ui_font_md(), 0);
    lv_obj_set_style_text_color(rl, UI_COL_WHITE, 0);
    lv_obj_center(rl);

    lv_obj_t *db = lv_btn_create(p);
    lv_obj_set_size(db, 110, 38);
    lv_obj_align(db, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
    lv_obj_set_style_bg_color(db, UI_COL_RED, 0);
    lv_obj_add_event_cb(db, detener_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "DETENER");
    lv_obj_set_style_text_font(dl, ui_font_md(), 0);
    lv_obj_set_style_text_color(dl, UI_COL_WHITE, 0);
    lv_obj_center(dl);

    // Ver nota en screen_func_programas.c — 500 → 1000 ms para no monopolizar
    // el render durante la operación y dejar más margen al polling de touch.
    lv_timer_create(update_cb, 1000, NULL);
}
