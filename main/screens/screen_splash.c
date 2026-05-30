#include "ui_common.h"
#include "img/img_assets.h"
#include "app_state.h"

#include <stdio.h>
#include "esp_log.h"

static lv_obj_t  *s_bar;
static lv_obj_t  *s_modelo_lbl;
static lv_obj_t  *s_serie_lbl;
static lv_timer_t *s_progress_timer;
static lv_obj_t  *s_scr;             // este screen, para no pisar al modal de recovery

#define SPLASH_DURATION_MS  5000
#define SPLASH_STEP_MS      50

static void progress_cb(lv_timer_t *t)
{
    int32_t v = lv_bar_get_value(s_bar);
    v += (100 * SPLASH_STEP_MS) / SPLASH_DURATION_MS;
    if (v >= 100) {
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        lv_timer_del(t);
        s_progress_timer = NULL;
        // Si el modal de power-fail recovery ya navegó a la pantalla de proceso
        // (operario tocó RECUPERAR antes de que terminara el splash), NO la
        // pisamos con INICIO. lv_scr_act() != s_scr cuando eso pasó.
        if (lv_scr_act() == s_scr) ui_show_screen(UI_SCREEN_INICIO);
        return;
    }
    lv_bar_set_value(s_bar, v, LV_ANIM_ON);
}

static void refresh_modelo_cb(lv_event_t *e)
{
    (void)e;
    char modelo[MODEL_NAME_MAX + 1];
    char serie[SERIE_NUM_MAX + 1];
    app_state_lock();
    snprintf(modelo, sizeof(modelo), "%s", app_state_get()->modelo);
    snprintf(serie,  sizeof(serie),  "%s", app_state_get()->serie);
    app_state_unlock();
    lv_label_set_text(s_modelo_lbl, modelo);
    lv_label_set_text(s_serie_lbl,  serie);
}

void screen_splash_build(lv_obj_t *scr)
{
    s_scr = scr;
    // White full-screen background, no left panel.
    lv_obj_set_style_bg_color(scr, UI_COL_WHITE, 0);

    // Logo (300×196) centrado arriba.
    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &logo_300);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 2);

    // Línea MODELO + valor en un container flex centrado. Esto hace que la
    // línea quede SIEMPRE centrada en el ancho de la pantalla, sin importar
    // el largo del nombre del modelo. Mismo patrón para N° SERIE abajo.
    lv_obj_t *modelo_row = lv_obj_create(scr);
    lv_obj_remove_style_all(modelo_row);
    lv_obj_set_size(modelo_row, LV_HOR_RES, 36);
    lv_obj_align(modelo_row, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_set_flex_flow(modelo_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modelo_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modelo_row, 14, 0);   // gap horizontal
    lv_obj_clear_flag(modelo_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *m1 = lv_label_create(modelo_row);
    lv_label_set_text(m1, "MODELO");
    lv_obj_set_style_text_font(m1, ui_font_xl(), 0);
    lv_obj_set_style_text_color(m1, UI_COL_BLACK, 0);

    s_modelo_lbl = lv_label_create(modelo_row);
    lv_obj_set_style_text_font(s_modelo_lbl, ui_font_xxl(), 0);
    lv_obj_set_style_text_color(s_modelo_lbl, UI_COL_ORANGE, 0);

    // Línea N° SERIE más chica, también centrada.
    lv_obj_t *serie_row = lv_obj_create(scr);
    lv_obj_remove_style_all(serie_row);
    lv_obj_set_size(serie_row, LV_HOR_RES, 24);
    lv_obj_align(serie_row, LV_ALIGN_TOP_MID, 0, 248);
    lv_obj_set_flex_flow(serie_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(serie_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(serie_row, 8, 0);
    lv_obj_clear_flag(serie_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *s1 = lv_label_create(serie_row);
    lv_label_set_text(s1, "N\xC2\xB0 SERIE:");
    lv_obj_set_style_text_font(s1, ui_font_md(), 0);
    lv_obj_set_style_text_color(s1, UI_COL_BLACK, 0);

    s_serie_lbl = lv_label_create(serie_row);
    lv_obj_set_style_text_font(s_serie_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_serie_lbl, UI_COL_BLUE, 0);

    lv_obj_add_event_cb(scr, refresh_modelo_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    refresh_modelo_cb(NULL);

    // URL pequeña arriba de la barra.
    lv_obj_t *url = lv_label_create(scr);
    lv_label_set_text(url, "www.bioorigen.com.ar   info@bioorigen.com.ar");
    lv_obj_set_style_text_font(url, ui_font_sm(), 0);
    lv_obj_set_style_text_color(url, UI_COL_BLUE, 0);
    lv_obj_align(url, LV_ALIGN_BOTTOM_MID, 0, -32);

    // Progress bar (orange)
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 360, 14);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COL_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    // Kick off the progress timer when this screen first loads.
    if (!s_progress_timer) {
        s_progress_timer = lv_timer_create(progress_cb, SPLASH_STEP_MS, NULL);
    }
}
