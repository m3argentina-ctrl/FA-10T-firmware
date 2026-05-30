#include "ui_common.h"
#include "app_state.h"
#include "safety.h"
#include "ssr3ch.h"

#include <stdio.h>

static lv_obj_t *s_fault_lbl;
static lv_obj_t *s_time_lbl;
static lv_obj_t *s_curr_lbl;
static lv_obj_t *s_temp_lbl;
static lv_obj_t *s_sess_lbl;

static const char *fault_name(uint32_t mask)
{
    if (mask & SAFETY_OVERTEMP)     return "SOBRETEMPERATURA";
    if (mask & SAFETY_RUNAWAY)      return "RUNAWAY TERMICO";
    if (mask & SAFETY_FAN_FAULT)    return "FALLA DE TURBINA";
    if (mask & SAFETY_SENSOR_FAULT) return "FALLA SENSOR PT1000";
    if (mask & SAFETY_WDT_TIMEOUT)  return "WATCHDOG TIMEOUT";
    return "SIN ALARMA";
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    app_state_lock();
    app_state_t *st = app_state_get();
    float t = st->last_sample.temperature;
    app_state_unlock();
    if (safety_request_clear(t)) {
        ui_show_screen(UI_SCREEN_INICIO);
    }
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    char buf[80];
    app_state_lock();
    const app_state_t s = *app_state_get();
    app_state_unlock();

    lv_label_set_text(s_fault_lbl, fault_name(s.safety_faults));
    snprintf(buf, sizeof(buf), "UPTIME %lus", (unsigned long)s.uptime_s);
    lv_label_set_text(s_time_lbl, buf);
    snprintf(buf, sizeof(buf), "I = %.3f A   /   NOM %.3f A", s.fan_current, s.fan_nominal);
    lv_label_set_text(s_curr_lbl, buf);
    snprintf(buf, sizeof(buf), "TEMP %.1f C", s.last_sample.temperature);
    lv_label_set_text(s_temp_lbl, buf);
    snprintf(buf, sizeof(buf), "SESION %lus", (unsigned long)s.session_elapsed_s);
    lv_label_set_text(s_sess_lbl, buf);
}

void screen_alarma_build(lv_obj_t *scr)
{
    ui_left_panel_attach(scr, UI_SCREEN_ALARMA);
    lv_obj_t *p = ui_make_right_pane(scr);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, LV_SYMBOL_WARNING "  ALARMA ACTIVA");
    lv_obj_set_style_text_font(title, ui_font_xl(), 0);
    lv_obj_set_style_text_color(title, UI_COL_RED, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_fault_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_fault_lbl, ui_font_xxl(), 0);
    lv_obj_set_style_text_color(s_fault_lbl, UI_COL_RED, 0);
    lv_obj_align(s_fault_lbl, LV_ALIGN_TOP_MID, 0, 44);
    lv_label_set_text(s_fault_lbl, "---");

    s_time_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_time_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_WHITE, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 88);
    lv_label_set_text(s_time_lbl, "");

    lv_obj_t *curr_box = ui_make_box(p, UI_COL_RED, UI_COL_YELLOW);
    lv_obj_set_size(curr_box, 320, 38);
    lv_obj_align(curr_box, LV_ALIGN_TOP_MID, 0, 116);
    s_curr_lbl = lv_label_create(curr_box);
    lv_obj_set_style_text_font(s_curr_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_curr_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_curr_lbl);

    lv_obj_t *action_box = ui_make_box(p, UI_COL_GREY_BTN, UI_COL_LABEL_GREY);
    lv_obj_set_size(action_box, 320, 32);
    lv_obj_align(action_box, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_t *action = lv_label_create(action_box);
    lv_label_set_text(action, "RESISTENCIAS Y TURBINAS DESACTIVADAS");
    lv_obj_set_style_text_font(action, ui_font_sm(), 0);
    lv_obj_set_style_text_color(action, UI_COL_WHITE, 0);
    lv_obj_center(action);

    s_temp_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_temp_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_temp_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_LEFT, 12, 200);

    s_sess_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_sess_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_sess_lbl, UI_COL_TIME_BLUE, 0);
    lv_obj_align(s_sess_lbl, LV_ALIGN_TOP_RIGHT, -12, 200);

    lv_obj_t *btn = lv_btn_create(p);
    lv_obj_set_size(btn, 280, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(btn, UI_COL_ORANGE, 0);
    lv_obj_add_event_cb(btn, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "CONFIRMAR Y REINICIAR EQUIPO");
    lv_obj_set_style_text_font(bl, ui_font_md(), 0);
    lv_obj_set_style_text_color(bl, UI_COL_WHITE, 0);
    lv_obj_center(bl);

    lv_obj_add_event_cb(scr, refresh_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
}
