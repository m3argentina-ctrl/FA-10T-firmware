#include "ui_common.h"
#include "wifi_config.h"
#include "wifi_manager.h"

static lv_obj_t *s_owner_scr;
static lv_obj_t *s_wifi_btn;
static lv_obj_t *s_wifi_btn_lbl;

static void manual_clicked(lv_event_t *e)    { (void)e; ui_show_screen(UI_SCREEN_PROG_MANUAL); }
static void programas_clicked(lv_event_t *e) { (void)e; ui_show_screen(UI_SCREEN_PROG_PROGRAMAS); }

// Botón WIFI: abre el modal de configuración de red. Lo conecta el cliente.
static void wifi_btn_cb(lv_event_t *e) { (void)e; wifi_config_show(); }

// Refresca el color/etiqueta del botón según el estado de conexión (verde =
// conectado, gris = sin conexión). Corre cada 1 s sólo si INICIO está visible.
static void wifi_status_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != s_owner_scr || !s_wifi_btn) return;
    bool conn = wifi_manager_is_connected();
    lv_obj_set_style_bg_color(s_wifi_btn, conn ? UI_COL_GREEN : UI_COL_GREY_BTN, 0);
    lv_label_set_text(s_wifi_btn_lbl, conn ? "WIFI OK" : "WIFI");
}

void screen_inicio_build(lv_obj_t *scr)
{
    s_owner_scr = scr;
    ui_left_panel_attach(scr, UI_SCREEN_INICIO);
    lv_obj_t *p = ui_make_right_pane(scr);

    // Marca textual (fondo oscuro → no usamos el isologo blanco aquí; va en panel izq).
    lv_obj_t *logo = lv_label_create(p);
    lv_label_set_text(logo, "bioOrigen");
    lv_obj_set_style_text_font(logo, ui_font_xxl(), 0);
    lv_obj_set_style_text_color(logo, UI_COL_ORANGE, 0);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 8);

    // Botón WIFI arriba a la derecha: lo conecta el cliente a su red. Hace de
    // indicador de estado (verde = conectado, gris = sin conexión). Se movió
    // desde AREA TECNICA para que el cliente no necesite el PIN de servicio.
    s_wifi_btn = lv_btn_create(p);
    lv_obj_set_size(s_wifi_btn, 60, 24);
    lv_obj_align(s_wifi_btn, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_set_style_bg_color(s_wifi_btn, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(s_wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    s_wifi_btn_lbl = lv_label_create(s_wifi_btn);
    lv_label_set_text(s_wifi_btn_lbl, "WIFI");
    lv_obj_set_style_text_font(s_wifi_btn_lbl, ui_font_xs(), 0);
    lv_obj_set_style_text_color(s_wifi_btn_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_wifi_btn_lbl);

    lv_obj_t *m1 = lv_label_create(p);
    lv_label_set_text(m1, "MODELO");
    lv_obj_set_style_text_font(m1, ui_font_md(), 0);
    lv_obj_set_style_text_color(m1, UI_COL_LABEL_GREY, 0);
    lv_obj_align(m1, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *m2 = lv_label_create(p);
    lv_label_set_text(m2, "IND-26MTO");
    lv_obj_set_style_text_font(m2, ui_font_xl(), 0);
    lv_obj_set_style_text_color(m2, UI_COL_ORANGE, 0);
    lv_obj_align(m2, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "MODOS DE OPERACION");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_WHITE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 124);

    // Two big mode buttons
    lv_obj_t *m_btn = lv_btn_create(p);
    lv_obj_set_size(m_btn, 130, 60);
    lv_obj_align(m_btn, LV_ALIGN_CENTER, -80, 70);
    lv_obj_set_style_bg_color(m_btn, UI_COL_ORANGE, 0);
    lv_obj_set_style_radius(m_btn, 8, 0);
    lv_obj_add_event_cb(m_btn, manual_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *m_lbl = lv_label_create(m_btn);
    lv_label_set_text(m_lbl, "MANUAL");
    lv_obj_set_style_text_font(m_lbl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(m_lbl, UI_COL_WHITE, 0);
    lv_obj_center(m_lbl);

    lv_obj_t *p_btn = lv_btn_create(p);
    lv_obj_set_size(p_btn, 130, 60);
    lv_obj_align(p_btn, LV_ALIGN_CENTER, 80, 70);
    lv_obj_set_style_bg_color(p_btn, UI_COL_GREEN, 0);
    lv_obj_set_style_radius(p_btn, 8, 0);
    lv_obj_add_event_cb(p_btn, programas_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p_lbl = lv_label_create(p_btn);
    lv_label_set_text(p_lbl, "PROGRAMAS");
    lv_obj_set_style_text_font(p_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(p_lbl, UI_COL_WHITE, 0);
    lv_obj_center(p_lbl);

    // Estado WiFi en vivo (verde/gris) mientras esta pantalla esté activa.
    lv_timer_create(wifi_status_cb, 1000, NULL);
}
