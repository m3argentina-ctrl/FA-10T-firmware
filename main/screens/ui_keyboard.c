// Modal de teclado on-screen reutilizable. Ver ui_keyboard.h.

#include "ui_keyboard.h"
#include "ui_common.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_kb";

static lv_obj_t              *s_modal;
static lv_obj_t              *s_title_lbl;
static lv_obj_t              *s_ta;
static lv_obj_t              *s_bm_text;     // botonera alfanumérica
static lv_obj_t              *s_bm_num;      // botonera numérica
static ui_keyboard_done_cb_t  s_done_cb;
static void                  *s_user_data;
static ui_keyboard_mode_t     s_mode;

// --- Maps de las dos botoneras ---
static const char *MAP_TEXT[] = {
    "A","B","C","D","E","F","G","H","I","J","\n",
    "K","L","M","N","O","P","Q","R","S","T","\n",
    "U","V","W","X","Y","Z","-","_",".",LV_SYMBOL_BACKSPACE,"\n",
    "0","1","2","3","4","5","6","7","8","9","\n",
    "SP","CANCELAR","OK",
    "",
};
// Índices en MAP_TEXT para botones especiales (sin contar separadores).
#define MAP_TEXT_SP_IDX        40
#define MAP_TEXT_CANCEL_IDX    41
#define MAP_TEXT_OK_IDX        42

static const char *MAP_NUM[] = {
    "1","2","3","\n",
    "4","5","6","\n",
    "7","8","9","\n",
    LV_SYMBOL_BACKSPACE,"0","OK","\n",
    "CANCELAR",
    "",
};

static void close_modal(void)
{
    if (s_modal) lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    s_done_cb   = NULL;
    s_user_data = NULL;
}

static void handle_key(const char *txt)
{
    if (!txt) return;
    if (strcmp(txt, "OK") == 0) {
        const char *t = lv_textarea_get_text(s_ta);
        ESP_LOGI(TAG, "OK -> '%s'", t);
        ui_keyboard_done_cb_t cb  = s_done_cb;
        void                 *ud  = s_user_data;
        // Cerrar primero por si el cb abre otro keyboard.
        close_modal();
        if (cb) cb(t, ud);
    } else if (strcmp(txt, "CANCELAR") == 0) {
        ESP_LOGI(TAG, "CANCELAR");
        close_modal();
    } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(s_ta);
    } else if (strcmp(txt, "SP") == 0) {
        lv_textarea_add_char(s_ta, ' ');
    } else {
        lv_textarea_add_text(s_ta, txt);
    }
}

static void bm_event_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    uint16_t  id = lv_btnmatrix_get_selected_btn(bm);
    const char *txt = lv_btnmatrix_get_btn_text(bm, id);
    handle_key(txt);
}

static void build_modal_once(void)
{
    if (s_modal) return;

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    s_title_lbl = lv_label_create(s_modal);
    lv_label_set_text(s_title_lbl, "EDITAR");
    lv_obj_set_style_text_font(s_title_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_title_lbl, UI_COL_GREEN, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_TOP_LEFT, 6, 4);

    s_ta = lv_textarea_create(s_modal);
    lv_obj_set_size(s_ta, LV_HOR_RES - 12, 30);
    lv_obj_align(s_ta, LV_ALIGN_TOP_LEFT, 6, 26);
    lv_textarea_set_one_line(s_ta, true);
    lv_obj_set_style_text_font(s_ta, ui_font_lg(), 0);
    lv_obj_set_style_bg_color(s_ta, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_text_color(s_ta, UI_COL_WHITE, 0);

    // Botonera alfanumérica (mode TEXT)
    s_bm_text = lv_btnmatrix_create(s_modal);
    lv_obj_set_size(s_bm_text, LV_HOR_RES - 8, LV_VER_RES - 64);
    lv_obj_align(s_bm_text, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_btnmatrix_set_map(s_bm_text, MAP_TEXT);
    lv_obj_set_style_text_font(s_bm_text, ui_font_md(), 0);
    lv_obj_set_style_pad_all(s_bm_text, 2, 0);
    lv_obj_set_style_pad_gap(s_bm_text, 2, 0);
    // Última fila: SP (4u) | CANCELAR (3u) | OK (3u)
    lv_btnmatrix_set_btn_width(s_bm_text, MAP_TEXT_SP_IDX,     4);
    lv_btnmatrix_set_btn_width(s_bm_text, MAP_TEXT_CANCEL_IDX, 3);
    lv_btnmatrix_set_btn_width(s_bm_text, MAP_TEXT_OK_IDX,     3);
    lv_obj_add_event_cb(s_bm_text, bm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Botonera numérica (mode NUMERIC)
    s_bm_num = lv_btnmatrix_create(s_modal);
    lv_obj_set_size(s_bm_num, 280, 200);
    lv_obj_align(s_bm_num, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_btnmatrix_set_map(s_bm_num, MAP_NUM);
    lv_obj_set_style_text_font(s_bm_num, ui_font_lg(), 0);
    lv_obj_add_event_cb(s_bm_num, bm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

void ui_keyboard_open(const char           *initial,
                       size_t                max_len,
                       const char           *title,
                       ui_keyboard_mode_t    mode,
                       ui_keyboard_done_cb_t done_cb,
                       void                 *user_data)
{
    build_modal_once();
    if (!lv_obj_has_flag(s_modal, LV_OBJ_FLAG_HIDDEN)) {
        ESP_LOGW(TAG, "open ignored: modal already visible");
        return;
    }
    s_done_cb   = done_cb;
    s_user_data = user_data;
    s_mode      = mode;

    lv_label_set_text(s_title_lbl, title ? title : "EDITAR");
    if (max_len > 0) lv_textarea_set_max_length(s_ta, (uint32_t)(max_len - 1));
    lv_textarea_set_password_mode(s_ta, false);
    lv_textarea_set_text(s_ta, initial ? initial : "");

    if (mode == UI_KB_MODE_NUMERIC) {
        lv_obj_add_flag(s_bm_text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_bm_num, LV_OBJ_FLAG_HIDDEN);
        // Para PIN: ocultar el texto tipeado.
        lv_textarea_set_password_mode(s_ta, true);
    } else {
        lv_obj_clear_flag(s_bm_text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_bm_num, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_modal);
}
