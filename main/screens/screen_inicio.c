#include "ui_common.h"
#include "img/img_assets.h"
#include "process_switch.h"
#include "wifi_config.h"
#include "wifi_manager.h"
#include "app_state.h"

#include <stdio.h>

static lv_obj_t *s_owner_scr;
static lv_obj_t *s_wifi_btn;
static lv_obj_t *s_wifi_btn_lbl;
static lv_obj_t *s_model_val_lbl;   // valor del MODELO (se lee de app_state)

// Refresca el valor del MODELO leyéndolo de app_state. Se dispara al ENTRAR a
// INICIO para reflejar un cambio hecho en AREA TECNICA sin reiniciar el equipo.
static void refresh_modelo_cb(lv_event_t *e)
{
    (void)e;
    if (!s_model_val_lbl) return;
    char modelo[MODEL_NAME_MAX + 1];
    app_state_lock();
    snprintf(modelo, sizeof(modelo), "%s", app_state_get()->modelo);
    app_state_unlock();
    lv_label_set_text(s_model_val_lbl, modelo);
}

// Pasan por el guard: si hay un proceso vivo, avisa antes de pisarlo.
static void manual_clicked(lv_event_t *e)    { (void)e; process_switch_request(UI_SCREEN_PROG_MANUAL); }
static void programas_clicked(lv_event_t *e) { (void)e; process_switch_request(UI_SCREEN_PROG_PROGRAMAS); }

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

    // Logo Bio Origen dentro de una TARJETA BLANCA: el PNG no tiene alpha y su
    // fondo es blanco (además el "volver a lo natural..." es oscuro), así que
    // sobre el panel negro quedaría un recuadro feo y la bajada ilegible.
    // La tarjeta lo integra y respeta los colores de marca.
    lv_obj_t *card = lv_obj_create(p);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 166, 111);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(card, UI_COL_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_img_create(card);
    lv_img_set_src(logo, &logo_inicio);   // 160×105
    lv_obj_center(logo);

    // Botón WIFI arriba a la derecha: lo conecta el cliente a su red. Hace de
    // indicador de estado (verde = conectado, gris = sin conexión). Se movió
    // desde AREA TECNICA para que el cliente no necesite el PIN de servicio.
    s_wifi_btn = lv_btn_create(p);
    lv_obj_set_size(s_wifi_btn, 78, 32);
    lv_obj_align(s_wifi_btn, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_set_style_bg_color(s_wifi_btn, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(s_wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    s_wifi_btn_lbl = lv_label_create(s_wifi_btn);
    lv_label_set_text(s_wifi_btn_lbl, "WIFI");
    lv_obj_set_style_text_font(s_wifi_btn_lbl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_wifi_btn_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_wifi_btn_lbl);

    lv_obj_t *m1 = lv_label_create(p);
    lv_label_set_text(m1, "MODELO");
    lv_obj_set_style_text_font(m1, ui_font_md(), 0);
    lv_obj_set_style_text_color(m1, UI_COL_LABEL_GREY, 0);
    lv_obj_align(m1, LV_ALIGN_TOP_MID, 0, 148);

    s_model_val_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_model_val_lbl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(s_model_val_lbl, UI_COL_ORANGE, 0);
    lv_obj_align(s_model_val_lbl, LV_ALIGN_TOP_MID, 0, 166);
    refresh_modelo_cb(NULL);   // carga el modelo actual desde app_state

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "MODOS DE OPERACION");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_WHITE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 200);

    // Two big mode buttons
    lv_obj_t *m_btn = lv_btn_create(p);
    lv_obj_set_size(m_btn, 130, 60);
    lv_obj_align(m_btn, LV_ALIGN_CENTER, -80, 98);
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
    lv_obj_align(p_btn, LV_ALIGN_CENTER, 80, 98);
    lv_obj_set_style_bg_color(p_btn, UI_COL_GREEN, 0);
    lv_obj_set_style_radius(p_btn, 8, 0);
    lv_obj_add_event_cb(p_btn, programas_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p_lbl = lv_label_create(p_btn);
    lv_label_set_text(p_lbl, "PROGRAMAS");
    lv_obj_set_style_text_font(p_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(p_lbl, UI_COL_WHITE, 0);
    lv_obj_center(p_lbl);

    // Refresca el MODELO al volver a esta pantalla (p.ej. tras editarlo en
    // AREA TECNICA), sin necesidad de reiniciar el equipo.
    lv_obj_add_event_cb(scr, refresh_modelo_cb, LV_EVENT_SCREEN_LOAD_START, NULL);

    // Estado WiFi en vivo (verde/gris) mientras esta pantalla esté activa.
    lv_timer_create(wifi_status_cb, 1000, NULL);
}
