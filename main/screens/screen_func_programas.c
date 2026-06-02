#include "ui_common.h"
#include "app_state.h"
#include "app_config.h"
#include "programa.h"
#include "safety.h"

#include <stdio.h>

static lv_obj_t *s_owner_scr;
static lv_obj_t *s_prog_bar, *s_prog_lbl;
static lv_obj_t *s_etapa_box[PROG_STAGE_COUNT];
static lv_obj_t *s_etapa_t_lbl[PROG_STAGE_COUNT];
static lv_obj_t *s_etapa_d_lbl[PROG_STAGE_COUNT];
static lv_obj_t *s_big_temp, *s_temp_sp_lbl;
static lv_obj_t *s_big_time, *s_time_total_lbl;
static lv_obj_t *s_temp_bar, *s_time_bar;
static lv_obj_t *s_res_box, *s_res_lbl;
static lv_obj_t *s_fan_box, *s_fan_lbl;
static lv_obj_t *s_kwh_lbl;
static lv_obj_t *s_pausar_btn;

static void format_hhmmss(uint32_t s, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

static void format_hhmm(uint32_t s, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60));
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

    char buf[48];

    // Barra superior con nombre + etapa actual
    if (s.run_state == RUN_STATE_PAUSED) {
        snprintf(buf, sizeof(buf), "PAUSADO  %s  E%u/%u", s.nombre_programa,
                 (unsigned)(s.etapa_activa + 1), (unsigned)PROG_STAGE_COUNT);
        lv_obj_set_style_bg_color(s_prog_bar, UI_COL_YELLOW, 0);
    } else if (s.run_state == RUN_STATE_COMPLETED) {
        snprintf(buf, sizeof(buf), "COMPLETADO  %s", s.nombre_programa);
        lv_obj_set_style_bg_color(s_prog_bar, UI_COL_GREEN, 0);
    } else if (!s.warmup_done) {
        snprintf(buf, sizeof(buf), "CALENTANDO  %s", s.nombre_programa);
        lv_obj_set_style_bg_color(s_prog_bar, UI_COL_ORANGE, 0);
    } else {
        snprintf(buf, sizeof(buf), "%s  —  ETAPA %u/%u", s.nombre_programa,
                 (unsigned)(s.etapa_activa + 1), (unsigned)PROG_STAGE_COUNT);
        lv_obj_set_style_bg_color(s_prog_bar, UI_COL_GREEN, 0);
    }
    lv_label_set_text(s_prog_lbl, buf);

    // 3 etapas
    for (int i = 0; i < PROG_STAGE_COUNT; ++i) {
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""C", s.etapa_sp[i]);
        lv_label_set_text(s_etapa_t_lbl[i], buf);
        format_hhmm(s.etapa_duration_s[i], buf, sizeof(buf));
        lv_label_set_text(s_etapa_d_lbl[i], buf);

        const bool active = (i == s.etapa_activa) && (s.run_state == RUN_STATE_RUNNING)
                            && s.warmup_done;
        lv_obj_set_style_bg_color(s_etapa_box[i],
                                  active ? UI_COL_ORANGE : UI_COL_PANEL_BG, 0);
        lv_obj_set_style_border_color(s_etapa_box[i],
                                      active ? UI_COL_YELLOW : UI_COL_GREY_BTN, 0);
    }

    // Big TEMP
    snprintf(buf, sizeof(buf), "%.1f", s.last_sample.temperature);
    lv_label_set_text(s_big_temp, buf);
    snprintf(buf, sizeof(buf), "/ %.0f \xC2\xB0""C", s.effective_setpoint);
    lv_label_set_text(s_temp_sp_lbl, buf);

    // Big TIEMPO
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

    // Bars
    int32_t t_pct = (int32_t)s.last_sample.temperature;
    if (t_pct < 0)   t_pct = 0;
    if (t_pct > 100) t_pct = 100;
    lv_bar_set_value(s_temp_bar, t_pct, LV_ANIM_OFF);

    int32_t pct = 0;
    if (s.session_total_s > 0 && s.warmup_done) {
        // uint64 en el producto: ver nota en screen_func_manual.c.
        pct = (int32_t)(((uint64_t)s.session_elapsed_s * 100) / s.session_total_s);
    }
    lv_bar_set_value(s_time_bar, pct, LV_ANIM_OFF);

    // ON/OFF
    set_onoff(s_res_box, s_res_lbl, s.ssr_drv_duty > 0.05f);
    set_onoff(s_fan_box, s_fan_lbl, s.ssr_fan_duty > 0.05f);

    // Consumo estimado del proceso (kWh): resistencia + turbina por módulo,
    // × num_modulos. Sube en vivo y queda fijo en el total al COMPLETAR.
    float wh_mod = s.session_energy_wh + s.session_fan_on_s * (FAN_WATTS_PER_MODULE / 3600.0f);
    snprintf(buf, sizeof(buf), "%.2f", wh_mod * (float)s.num_modulos / 1000.0f);
    lv_label_set_text(s_kwh_lbl, buf);

    if (s.safety_faults & SAFETY_TRIP_MASK) { ui_show_screen(UI_SCREEN_ALARMA); return; }

    // Botón pausar adapta color según estado
    if (s.run_state == RUN_STATE_PAUSED || s.run_state == RUN_STATE_COMPLETED) {
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_GREY_BTN, 0);
    } else {
        lv_obj_set_style_bg_color(s_pausar_btn, UI_COL_ORANGE, 0);
    }
}

// --- Botones ---
static void pausar_cb(lv_event_t *e)    { (void)e; programa_pause();  }
static void reiniciar_cb(lv_event_t *e) { (void)e; programa_resume(); }
static void detener_cb(lv_event_t *e)
{
    (void)e;
    programa_stop();
    ui_show_screen(UI_SCREEN_PROG_PROGRAMAS);
}

// --- Cajón con leyenda y valor (h=40 evita overlap). ---
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
    lv_obj_set_style_text_font(cap, ui_font_sm_bold(), 0);   // ver screen_func_manual.c
    lv_obj_set_style_text_color(cap, UI_COL_WHITE, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *val = lv_label_create(b);
    lv_obj_set_style_text_font(val, ui_font_lg_bold(), 0);  // bold 18, ver screen_func_manual.c
    lv_obj_set_style_text_color(val, value_col, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(val, "-");
    *out_value = val;
    return b;
}

void screen_func_programas_build(lv_obj_t *scr)
{
    s_owner_scr = scr;
    ui_left_panel_attach(scr, UI_SCREEN_PROG_PROGRAMAS);
    lv_obj_t *p = ui_make_right_pane(scr);

    // y=0..20: barra con nombre programa + etapa
    s_prog_bar = lv_obj_create(p);
    lv_obj_remove_style_all(s_prog_bar);
    lv_obj_set_size(s_prog_bar, LV_HOR_RES - UI_RIGHT_X, 20);
    lv_obj_align(s_prog_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_prog_bar, UI_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(s_prog_bar, LV_OPA_COVER, 0);
    s_prog_lbl = lv_label_create(s_prog_bar);
    lv_obj_set_style_text_font(s_prog_lbl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_prog_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_prog_lbl);
    lv_label_set_text(s_prog_lbl, "PROGRAMA: -");

    // y=24..64: row de 3 etapas (114 wide × 40 alto, gaps 6)
    const int E_W = 114, E_H = 40, E_GAP = 6;
    for (int i = 0; i < PROG_STAGE_COUNT; ++i) {
        s_etapa_box[i] = ui_make_box(p, UI_COL_PANEL_BG, UI_COL_GREY_BTN);
        lv_obj_set_size(s_etapa_box[i], E_W, E_H);
        lv_obj_align(s_etapa_box[i], LV_ALIGN_TOP_LEFT,
                     3 + i * (E_W + E_GAP), 24);
        lv_obj_set_style_pad_all(s_etapa_box[i], 2, 0);

        char hdr[12];
        snprintf(hdr, sizeof(hdr), "ETAPA %d", i + 1);
        lv_obj_t *h = lv_label_create(s_etapa_box[i]);
        lv_label_set_text(h, hdr);
        lv_obj_set_style_text_font(h, ui_font_sm_bold(), 0);   // Bold 12
        lv_obj_set_style_text_color(h, UI_COL_WHITE, 0);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);

        s_etapa_t_lbl[i] = lv_label_create(s_etapa_box[i]);
        lv_obj_set_style_text_font(s_etapa_t_lbl[i], ui_font_md_bold(), 0);  // Bold 14
        lv_obj_set_style_text_color(s_etapa_t_lbl[i], UI_COL_TEMP_RED, 0);
        lv_obj_align(s_etapa_t_lbl[i], LV_ALIGN_BOTTOM_LEFT, 2, 0);
        lv_label_set_text(s_etapa_t_lbl[i], "-");

        s_etapa_d_lbl[i] = lv_label_create(s_etapa_box[i]);
        lv_obj_set_style_text_font(s_etapa_d_lbl[i], ui_font_md_bold(), 0);
        lv_obj_set_style_text_color(s_etapa_d_lbl[i], UI_COL_TIME_BLUE, 0);
        lv_obj_align(s_etapa_d_lbl[i], LV_ALIGN_BOTTOM_RIGHT, -2, 0);
        lv_label_set_text(s_etapa_d_lbl[i], "-");
    }

    // y=70..118: TEMPERATURA grande + label SP + barra
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

    s_temp_bar = lv_bar_create(p);
    lv_obj_set_size(s_temp_bar, 340, 12);
    lv_obj_align(s_temp_bar, LV_ALIGN_TOP_LEFT, 8, 120);
    lv_bar_set_range(s_temp_bar, 0, 100);
    lv_obj_set_style_bg_color(s_temp_bar, UI_COL_GREY_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_temp_bar, UI_COL_TEMP_RED, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_temp_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_temp_bar, 5, LV_PART_INDICATOR);

    // y=138..186: TIEMPO grande + label total + barra
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

    s_time_bar = lv_bar_create(p);
    lv_obj_set_size(s_time_bar, 340, 12);
    lv_obj_align(s_time_bar, LV_ALIGN_TOP_LEFT, 8, 188);
    lv_bar_set_range(s_time_bar, 0, 100);
    lv_obj_set_style_bg_color(s_time_bar, UI_COL_GREY_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_time_bar, UI_COL_TIME_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_time_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_time_bar, 5, LV_PART_INDICATOR);

    // y=208..248: row RESISTENCIA | TURBINAS | CONSUMO (3 cajas centradas)
    const int B2_W = 110, B2_GAP = 9;
    int x2 = (LV_HOR_RES - UI_RIGHT_X - (3 * B2_W + 2 * B2_GAP)) / 2;
    s_res_box = make_info_box(p, "RESISTENCIA", x2,                     208, B2_W,
                              UI_COL_GREEN, UI_COL_WHITE, &s_res_lbl);
    s_fan_box = make_info_box(p, "TURBINAS",    x2 + (B2_W + B2_GAP),   208, B2_W,
                              UI_COL_GREEN, UI_COL_WHITE, &s_fan_lbl);
    make_info_box(p, "CONSUMO kWh", x2 + 2*(B2_W + B2_GAP), 208, B2_W,
                  UI_COL_GREEN, UI_COL_GREEN, &s_kwh_lbl);

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

    // 500 → 1000 ms: bajamos la carga de render a la mitad. El usuario no
    // percibe diferencia (HMI industrial, temperatura/tiempo cambian < 1 Hz)
    // pero la ventana de polling de touch queda mucho más limpia.
    lv_timer_create(update_cb, 1000, NULL);
}
