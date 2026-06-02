#include "process_switch.h"
#include "ui_common.h"
#include "app_state.h"
#include "modo_manual.h"
#include "programa.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "proc_switch";

static lv_obj_t  *s_modal;
static ui_screen_id_t s_target;     // a dónde quería ir el usuario
static op_mode_t  s_live_mode;      // tipo de la sesión en curso

static void close_modal(void)
{
    if (s_modal) { lv_obj_del(s_modal); s_modal = NULL; }
}

// Pantalla FUNC en vivo según el modo de la sesión en curso.
static ui_screen_id_t func_screen_for(op_mode_t mode)
{
    return (mode == OP_MODE_PROGRAMS) ? UI_SCREEN_FUNC_PROGRAMAS
                                      : UI_SCREEN_FUNC_MANUAL;
}

// CONTINUAR: no toca el proceso, vuelve a su pantalla de monitoreo.
static void continuar_cb(lv_event_t *e)
{
    (void)e;
    op_mode_t mode = s_live_mode;
    close_modal();
    ui_show_screen(func_screen_for(mode));
    ESP_LOGI(TAG, "operario eligió CONTINUAR el proceso en curso (mode=%d)",
             (int)mode);
}

// DETENER: corta la sesión viva y recién entonces navega al destino pedido.
static void detener_cb(lv_event_t *e)
{
    (void)e;
    if (s_live_mode == OP_MODE_PROGRAMS) programa_stop();
    else                                 modo_manual_stop();
    ui_screen_id_t target = s_target;
    close_modal();
    ui_show_screen(target);
    ESP_LOGI(TAG, "operario eligió DETENER el proceso en curso → navega a %d",
             (int)target);
}

void process_switch_request(ui_screen_id_t target)
{
    // Lee el estado vivo bajo lock.
    op_mode_t   mode;
    run_state_t st;
    app_state_lock();
    {
        app_state_t *s = app_state_get();
        mode = s->op_mode;
        st   = s->run_state;
    }
    app_state_unlock();

    bool live = (mode != OP_MODE_IDLE) &&
                (st == RUN_STATE_RUNNING || st == RUN_STATE_PAUSED);

    // Sin sesión viva → navegación normal, sin molestar.
    if (!live) {
        ui_show_screen(target);
        return;
    }

    // Si ya hay un modal abierto (doble toque), no apilar otro.
    if (s_modal) return;

    s_target    = target;
    s_live_mode = mode;

    // Fondo opaco a pantalla completa.
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, LV_SYMBOL_WARNING "  PROCESO EN CURSO");
    lv_obj_set_style_text_font(title, ui_font_xl(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    // Cuerpo: qué hay en curso + la pregunta.
    char buf[140];
    snprintf(buf, sizeof(buf),
             "Hay un proceso %s en curso.\n\n"
             "¿Detenerlo para iniciar otro,\no continuar con el actual?",
             (s_live_mode == OP_MODE_PROGRAMS) ? "de PROGRAMA" : "MANUAL");
    lv_obj_t *body = lv_label_create(s_modal);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 384);
    lv_label_set_text(body, buf);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, ui_font_md(), 0);
    lv_obj_set_style_text_color(body, UI_COL_WHITE, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 78);

    // CONTINUAR (verde, izquierda) — opción segura por defecto.
    lv_obj_t *cb = lv_btn_create(s_modal);
    lv_obj_set_size(cb, 210, 56);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_LEFT, 18, -16);
    lv_obj_set_style_bg_color(cb, UI_COL_GREEN, 0);
    lv_obj_add_event_cb(cb, continuar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cb);
    lv_label_set_text(cl, "CONTINUAR");
    lv_obj_set_style_text_font(cl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(cl, UI_COL_WHITE, 0);
    lv_obj_center(cl);

    // DETENER (rojo, derecha) — acción destructiva del proceso en curso.
    lv_obj_t *db = lv_btn_create(s_modal);
    lv_obj_set_size(db, 210, 56);
    lv_obj_align(db, LV_ALIGN_BOTTOM_RIGHT, -18, -16);
    lv_obj_set_style_bg_color(db, UI_COL_RED, 0);
    lv_obj_add_event_cb(db, detener_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "DETENER");
    lv_obj_set_style_text_font(dl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(dl, UI_COL_WHITE, 0);
    lv_obj_center(dl);

    lv_obj_move_foreground(s_modal);
    ESP_LOGI(TAG, "modal cambio-de-proceso: live mode=%d state=%d → target=%d",
             (int)s_live_mode, (int)st, (int)s_target);
}
