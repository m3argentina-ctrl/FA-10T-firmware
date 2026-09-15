#include "autotune_ui.h"
#include "ui_common.h"
#include "app_config.h"
#include "app_state.h"
#include "autotune.h"
#include "nvs_config.h"
#include "telemetry.h"
#include "tune_store.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "autotune_ui";

#define MSG_HOLD_MS     4000
#define FACTORY_ARM_MS  4000

static lv_obj_t   *s_modal;
static lv_obj_t   *s_cur_lbl;
static lv_obj_t   *s_info_lbl;
static lv_obj_t   *s_sp_row, *s_sp_lbl;
static lv_obj_t   *s_btn_main, *s_btn_main_lbl;   // INICIAR / CANCELAR / ACEPTAR
static lv_obj_t   *s_btn_alt,  *s_btn_alt_lbl;    // PID DE FABRICA / DESCARTAR
static lv_obj_t   *s_btn_close;
static lv_timer_t *s_timer;
static float       s_sp = AUTOTUNE_SP_DEFAULT_C;
static uint32_t    s_msg_until;
static uint32_t    s_factory_armed_until;

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool msg_active(void) { return (int32_t)(s_msg_until - lv_tick_get()) > 0; }

static void flash_msg(const char *txt)
{
    lv_label_set_text(s_info_lbl, txt);
    lv_obj_set_style_text_color(s_info_lbl, UI_COL_YELLOW, 0);
    s_msg_until = lv_tick_get() + MSG_HOLD_MS;
}

static void refresh(void)
{
    autotune_status_t st;
    autotune_get_status(&st);
    fa10t_config_t cfg;
    app_state_copy_config(&cfg);
    char buf[240];

    snprintf(buf, sizeof buf, "Actual: Kp %.3f   Ki %.5f   Kd %.2f   (%s)",
             cfg.kp, cfg.ki, cfg.kd, autotune_is_tuned() ? "autotune" : "fabrica");
    lv_label_set_text(s_cur_lbl, buf);

    const bool armed = (int32_t)(s_factory_armed_until - lv_tick_get()) > 0;
    snprintf(buf, sizeof buf, "SP de prueba: %.0f \xC2\xB0""C", s_sp);
    lv_label_set_text(s_sp_lbl, buf);

    show(s_sp_row,    st.state == AUTOTUNE_IDLE);
    show(s_btn_main,  st.state != AUTOTUNE_ABORTED);
    show(s_btn_alt,   st.state == AUTOTUNE_IDLE || st.state == AUTOTUNE_DONE);
    show(s_btn_close, st.state == AUTOTUNE_IDLE || st.state == AUTOTUNE_ABORTED);

    switch (st.state) {
    case AUTOTUNE_IDLE:
        lv_label_set_text(s_btn_main_lbl, "INICIAR");
        lv_obj_set_style_bg_color(s_btn_main, UI_COL_GREEN, 0);
        lv_label_set_text(s_btn_alt_lbl, armed ? "CONFIRMAR" : "PID DE FABRICA");
        lv_obj_set_style_bg_color(s_btn_alt, armed ? UI_COL_RED : UI_COL_GREY_BTN, 0);
        if (msg_active()) return;
        lv_obj_set_style_text_color(s_info_lbl, UI_COL_WHITE, 0);
        lv_label_set_text(s_info_lbl,
            "Camara vacia y cerrada. Las turbinas quedan prendidas y las resistencias se "
            "prenden y apagan alrededor del SP de prueba hasta medir 4 ciclos parejos. "
            "Tarda entre 20 y 60 min.");
        break;

    case AUTOTUNE_HEATING:
    case AUTOTUNE_RELAY: {
        lv_label_set_text(s_btn_main_lbl, "CANCELAR");
        lv_obj_set_style_bg_color(s_btn_main, UI_COL_RED, 0);
        const unsigned el = (unsigned)st.elapsed_s;
        snprintf(buf, sizeof buf,
                 "%s\n\nT %.1f \xC2\xB0""C     SP %.0f \xC2\xB0""C     Resistencias %.0f %%\n"
                 "Ciclos medidos %d de %d     Tiempo %02u:%02u",
                 st.state == AUTOTUNE_HEATING ? "CALENTANDO HASTA EL SP..." : "MIDIENDO LA OSCILACION...",
                 st.temp, st.sp, st.duty * 100.0f, st.cycles, AUTOTUNE_CYCLES, el / 60, el % 60);
        lv_obj_set_style_text_color(s_info_lbl, UI_COL_CYAN, 0);
        lv_label_set_text(s_info_lbl, buf);
        break;
    }

    case AUTOTUNE_DONE:
        lv_label_set_text(s_btn_main_lbl, "ACEPTAR");
        lv_obj_set_style_bg_color(s_btn_main, UI_COL_GREEN, 0);
        lv_label_set_text(s_btn_alt_lbl, "DESCARTAR");
        lv_obj_set_style_bg_color(s_btn_alt, UI_COL_GREY_BTN, 0);
        if (msg_active()) return;
        snprintf(buf, sizeof buf,
                 "AUTOTUNE TERMINADO\n\nNuevo:  Kp %.3f   Ki %.5f   Kd %.2f\n"
                 "(Ku %.3f, periodo %.0f s)\n\nACEPTAR guarda estas ganancias en el equipo.",
                 st.kp, st.ki, st.kd, st.ku, st.pu);
        lv_obj_set_style_text_color(s_info_lbl, UI_COL_GREEN, 0);
        lv_label_set_text(s_info_lbl, buf);
        break;

    case AUTOTUNE_ABORTED:
        snprintf(buf, sizeof buf, "AUTOTUNE CANCELADO\n\n%s\n\nNo se cambiaron las ganancias.", st.reason);
        lv_obj_set_style_text_color(s_info_lbl, UI_COL_RED, 0);
        lv_label_set_text(s_info_lbl, buf);
        break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void apply_gains(float kp, float ki, float kd)
{
    fa10t_config_t cfg;
    app_state_copy_config(&cfg);
    cfg.kp = kp;
    cfg.ki = ki;
    cfg.kd = kd;
    app_state_set_config(&cfg);
    nvs_config_save(&cfg);
}

static void main_cb(lv_event_t *e)
{
    (void)e;
    autotune_status_t st;
    autotune_get_status(&st);

    if (st.state == AUTOTUNE_IDLE) {
        esp_err_t err = autotune_start(s_sp);
        if (err != ESP_OK) {
            flash_msg("No se puede iniciar: hay un proceso en marcha, una alarma o una sonda en falla.");
        }
    } else if (st.state == AUTOTUNE_HEATING || st.state == AUTOTUNE_RELAY) {
        autotune_cancel("CANCELADO POR EL OPERARIO");
    } else if (st.state == AUTOTUNE_DONE) {
        if (tune_store_save_pid(st.kp, st.ki, st.kd) != ESP_OK) {
            flash_msg("No se pudo guardar. Reintenta.");
            return;
        }
        apply_gains(st.kp, st.ki, st.kd);
        autotune_set_tuned(true);
        autotune_ack();
        ESP_LOGW(TAG, "PID de autotune aplicado: Kp=%.4f Ki=%.6f Kd=%.3f", st.kp, st.ki, st.kd);
        telemetry_log_event(TELEM_EVT_SERVICE, "PID autotune aplicado");
    }
    refresh();
}

static void alt_cb(lv_event_t *e)
{
    (void)e;
    autotune_status_t st;
    autotune_get_status(&st);

    if (st.state == AUTOTUNE_DONE) {
        autotune_ack();
    } else if (st.state == AUTOTUNE_IDLE) {
        if ((int32_t)(s_factory_armed_until - lv_tick_get()) <= 0) {
            s_factory_armed_until = lv_tick_get() + FACTORY_ARM_MS;   // segundo toque confirma
        } else {
            s_factory_armed_until = 0;
            fa10t_config_t def;
            nvs_config_defaults(&def);
            tune_store_clear_pid();
            apply_gains(def.kp, def.ki, def.kd);
            autotune_set_tuned(false);
            ESP_LOGW(TAG, "PID de fabrica restaurado");
            telemetry_log_event(TELEM_EVT_SERVICE, "PID de fabrica restaurado");
            flash_msg("PID de fabrica restaurado.");
        }
    }
    refresh();
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    autotune_ack();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_modal) { lv_obj_del(s_modal);   s_modal = NULL; }
}

static void sp_step(float delta)
{
    s_sp += delta;
    if (s_sp < AUTOTUNE_SP_MIN_C) s_sp = AUTOTUNE_SP_MIN_C;
    if (s_sp > AUTOTUNE_SP_MAX_C) s_sp = AUTOTUNE_SP_MAX_C;
    refresh();
}

static void sp_minus_cb(lv_event_t *e) { (void)e; sp_step(-AUTOTUNE_SP_STEP_C); }
static void sp_plus_cb(lv_event_t *e)  { (void)e; sp_step(+AUTOTUNE_SP_STEP_C); }

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_color_t col, int w, int h,
                          const lv_font_t *font, lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, UI_COL_WHITE, 0);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

void autotune_ui_show(void)
{
    if (s_modal) return;
    s_msg_until = 0;
    s_factory_armed_until = 0;

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, "AUTOTUNE PID");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    s_cur_lbl = lv_label_create(s_modal);
    lv_obj_set_style_text_font(s_cur_lbl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_cur_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(s_cur_lbl, LV_ALIGN_TOP_MID, 0, 34);

    s_info_lbl = lv_label_create(s_modal);
    lv_label_set_long_mode(s_info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_info_lbl, 456);
    lv_obj_set_style_text_align(s_info_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_info_lbl, ui_font_md(), 0);
    lv_obj_align(s_info_lbl, LV_ALIGN_TOP_MID, 0, 60);

    s_sp_row = lv_obj_create(s_modal);
    lv_obj_remove_style_all(s_sp_row);
    lv_obj_set_size(s_sp_row, 320, 46);
    lv_obj_align(s_sp_row, LV_ALIGN_TOP_MID, 0, 196);
    lv_obj_clear_flag(s_sp_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *minus = make_btn(s_sp_row, "-", UI_COL_GREY_BTN, 54, 42, ui_font_xl(), sp_minus_cb, NULL);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 0, 0);
    s_sp_lbl = lv_label_create(s_sp_row);
    lv_obj_set_style_text_font(s_sp_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_sp_lbl, UI_COL_WHITE, 0);
    lv_obj_align(s_sp_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *plus = make_btn(s_sp_row, "+", UI_COL_GREEN, 54, 42, ui_font_xl(), sp_plus_cb, NULL);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);

    s_btn_main = make_btn(s_modal, "INICIAR", UI_COL_GREEN, 200, 44, ui_font_md(), main_cb, &s_btn_main_lbl);
    lv_obj_align(s_btn_main, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    s_btn_alt = make_btn(s_modal, "PID DE FABRICA", UI_COL_GREY_BTN, 130, 44, ui_font_sm(), alt_cb, &s_btn_alt_lbl);
    lv_obj_align(s_btn_alt, LV_ALIGN_BOTTOM_LEFT, 222, -10);
    s_btn_close = make_btn(s_modal, "CERRAR", UI_COL_RED, 110, 44, ui_font_md(), close_cb, NULL);
    lv_obj_align(s_btn_close, LV_ALIGN_BOTTOM_RIGHT, -14, -10);

    refresh();
    s_timer = lv_timer_create(tick_cb, 500, NULL);
    lv_obj_move_foreground(s_modal);
}
