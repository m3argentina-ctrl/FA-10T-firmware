#include "display.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui";

static lv_obj_t *s_lbl_temp;
static lv_obj_t *s_lbl_sp;
static lv_obj_t *s_bar_duty;
static lv_obj_t *s_lbl_duty;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_uptime;

static lv_style_t s_style_big;
static lv_style_t s_style_status_ok;
static lv_style_t s_style_status_err;

static void build_styles(void)
{
    lv_style_init(&s_style_big);
    lv_style_set_text_font(&s_style_big, &lv_font_montserrat_48);
    lv_style_set_text_color(&s_style_big, lv_color_white());

    lv_style_init(&s_style_status_ok);
    lv_style_set_text_color(&s_style_status_ok, lv_palette_main(LV_PALETTE_GREEN));

    lv_style_init(&s_style_status_err);
    lv_style_set_text_color(&s_style_status_err, lv_palette_main(LV_PALETTE_RED));
}

void display_build_main_screen(void)
{
    if (display_lvgl_lock(200) != ESP_OK) return;

    build_styles();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "FA-10T v3.0");
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    // Temperature (large)
    s_lbl_temp = lv_label_create(scr);
    lv_obj_add_style(s_lbl_temp, &s_style_big, 0);
    lv_label_set_text(s_lbl_temp, "---.- °C");
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_MID, 0, 36);

    // Setpoint
    s_lbl_sp = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_sp, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(s_lbl_sp, "SP: ---.- °C");
    lv_obj_align(s_lbl_sp, LV_ALIGN_TOP_MID, 0, 96);

    // Output bar
    s_bar_duty = lv_bar_create(scr);
    lv_obj_set_size(s_bar_duty, 220, 18);
    lv_bar_set_range(s_bar_duty, 0, 1000);
    lv_obj_align(s_bar_duty, LV_ALIGN_BOTTOM_MID, 0, -40);

    s_lbl_duty = lv_label_create(scr);
    lv_label_set_text(s_lbl_duty, "OUT 0 %");
    lv_obj_set_style_text_color(s_lbl_duty, lv_color_white(), 0);
    lv_obj_align_to(s_lbl_duty, s_bar_duty, LV_ALIGN_OUT_TOP_MID, 0, -4);

    // Status line
    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "OK");
    lv_obj_add_style(s_lbl_status, &s_style_status_ok, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    // Uptime
    s_lbl_uptime = lv_label_create(scr);
    lv_label_set_text(s_lbl_uptime, "up 0s");
    lv_obj_set_style_text_color(s_lbl_uptime, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_align(s_lbl_uptime, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    display_lvgl_unlock();
    ESP_LOGI(TAG, "main screen built");
}

void display_update_status(const display_status_t *st)
{
    if (!st) return;
    if (display_lvgl_lock(50) != ESP_OK) return;

    char buf[48];

    if (st->sensor_fault) {
        lv_label_set_text(s_lbl_temp, "FAULT");
    } else {
        snprintf(buf, sizeof(buf), "%6.1f °C", st->temperature);
        lv_label_set_text(s_lbl_temp, buf);
    }

    snprintf(buf, sizeof(buf), "SP: %5.1f °C", st->setpoint);
    lv_label_set_text(s_lbl_sp, buf);

    int duty_pct_x10 = (int)(st->ssr_duty * 1000.0f + 0.5f);
    if (duty_pct_x10 < 0) duty_pct_x10 = 0;
    if (duty_pct_x10 > 1000) duty_pct_x10 = 1000;
    lv_bar_set_value(s_bar_duty, duty_pct_x10, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "OUT %d %%", duty_pct_x10 / 10);
    lv_label_set_text(s_lbl_duty, buf);

    if (st->runaway || st->safety_tripped) {
        lv_label_set_text(s_lbl_status, st->runaway ? "RUNAWAY" : "SAFETY TRIP");
        lv_obj_remove_style(s_lbl_status, &s_style_status_ok, 0);
        lv_obj_add_style(s_lbl_status, &s_style_status_err, 0);
    } else if (st->sensor_fault) {
        lv_label_set_text(s_lbl_status, "SENSOR");
        lv_obj_remove_style(s_lbl_status, &s_style_status_ok, 0);
        lv_obj_add_style(s_lbl_status, &s_style_status_err, 0);
    } else {
        lv_label_set_text(s_lbl_status, "OK");
        lv_obj_remove_style(s_lbl_status, &s_style_status_err, 0);
        lv_obj_add_style(s_lbl_status, &s_style_status_ok, 0);
    }

    snprintf(buf, sizeof(buf), "up %lus", (unsigned long)st->uptime_s);
    lv_label_set_text(s_lbl_uptime, buf);

    display_lvgl_unlock();
}
