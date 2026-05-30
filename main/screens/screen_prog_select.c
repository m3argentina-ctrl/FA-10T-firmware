// Pantalla UI_SCREEN_PROG_PROGRAMAS: grilla de 6 botones (PROG 1..6).
// - Slot vacío:    tap → wizard de edición directo.
// - Slot guardado: tap → modal con EDITAR / INICIAR / CANCELAR.

#include "ui_common.h"
#include "prog_wizard.h"
#include "programa.h"

#include <stdio.h>

static lv_obj_t *s_modal;          // overlay modal, oculto por default
static lv_obj_t *s_modal_title;
static lv_obj_t *s_modal_summary;
static uint8_t   s_modal_slot;

// --- Modal handlers ---
static void modal_close(void)
{
    if (s_modal) lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void modal_editar_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
    prog_wizard_enter(s_modal_slot);
}

static void modal_iniciar_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
    prog_wizard_start_slot(s_modal_slot);
}

static void modal_cancelar_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
}

static void modal_open(uint8_t slot, const programa_t *p)
{
    s_modal_slot = slot;

    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "PROG %u — %s",
             (unsigned)(slot + 1), p->nombre);
    lv_label_set_text(s_modal_title, title_buf);

    uint32_t total = p->etapa_duration_s[0] + p->etapa_duration_s[1]
                   + p->etapa_duration_s[2];
    char sum_buf[80];
    snprintf(sum_buf, sizeof(sum_buf),
             "E1 %.0f\xC2\xB0""C %02lu:%02lu   "
             "E2 %.0f\xC2\xB0""C %02lu:%02lu   "
             "E3 %.0f\xC2\xB0""C %02lu:%02lu\n"
             "TOTAL %02lu:%02lu",
             p->etapa_sp[0], (unsigned long)(p->etapa_duration_s[0] / 3600),
                              (unsigned long)((p->etapa_duration_s[0] / 60) % 60),
             p->etapa_sp[1], (unsigned long)(p->etapa_duration_s[1] / 3600),
                              (unsigned long)((p->etapa_duration_s[1] / 60) % 60),
             p->etapa_sp[2], (unsigned long)(p->etapa_duration_s[2] / 3600),
                              (unsigned long)((p->etapa_duration_s[2] / 60) % 60),
             (unsigned long)(total / 3600),
             (unsigned long)((total / 60) % 60));
    lv_label_set_text(s_modal_summary, sum_buf);

    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_modal);
}

static void slot_clicked_cb(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    programa_t p;
    bool exists = (programa_load(slot, &p) == ESP_OK) && p.used;
    if (exists) {
        modal_open(slot, &p);
    } else {
        // Slot vacío → directo al wizard de edición.
        prog_wizard_enter(slot);
    }
}

// --- Modal layout ---
static void build_modal(lv_obj_t *parent)
{
    s_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_80, 0);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_modal);
    lv_obj_set_size(card, 380, 240);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_modal_title = lv_label_create(card);
    lv_obj_set_style_text_font(s_modal_title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_modal_title, UI_COL_GREEN, 0);
    lv_obj_align(s_modal_title, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(s_modal_title, "PROG -");

    s_modal_summary = lv_label_create(card);
    lv_obj_set_style_text_font(s_modal_summary, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_modal_summary, UI_COL_WHITE, 0);
    lv_label_set_long_mode(s_modal_summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_modal_summary, 360);
    lv_obj_set_style_text_align(s_modal_summary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_modal_summary, LV_ALIGN_TOP_MID, 0, 36);
    lv_label_set_text(s_modal_summary, "");

    // 3 botones: EDITAR (azul) | INICIAR (verde) | CANCELAR (gris)
    lv_obj_t *editar = lv_btn_create(card);
    lv_obj_set_size(editar, 110, 44);
    lv_obj_align(editar, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(editar, UI_COL_BLUE, 0);
    lv_obj_add_event_cb(editar, modal_editar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(editar);
    lv_label_set_text(el, "EDITAR");
    lv_obj_set_style_text_font(el, ui_font_lg(), 0);
    lv_obj_set_style_text_color(el, UI_COL_WHITE, 0);
    lv_obj_center(el);

    lv_obj_t *iniciar = lv_btn_create(card);
    lv_obj_set_size(iniciar, 110, 44);
    lv_obj_align(iniciar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(iniciar, UI_COL_GREEN, 0);
    lv_obj_add_event_cb(iniciar, modal_iniciar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(iniciar);
    lv_label_set_text(il, "INICIAR");
    lv_obj_set_style_text_font(il, ui_font_lg(), 0);
    lv_obj_set_style_text_color(il, UI_COL_WHITE, 0);
    lv_obj_center(il);

    lv_obj_t *cancelar = lv_btn_create(card);
    lv_obj_set_size(cancelar, 110, 44);
    lv_obj_align(cancelar, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(cancelar, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(cancelar, modal_cancelar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancelar);
    lv_label_set_text(cl, "CANCELAR");
    lv_obj_set_style_text_font(cl, ui_font_md(), 0);
    lv_obj_set_style_text_color(cl, UI_COL_WHITE, 0);
    lv_obj_center(cl);

    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

void screen_prog_select_build(lv_obj_t *scr)
{
    ui_left_panel_attach(scr, UI_SCREEN_PROG_PROGRAMAS);
    lv_obj_t *p = ui_make_right_pane(scr);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "SELECCIONAR PROGRAMA");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_GREEN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Grilla 3 cols × 2 filas. Right pane = 360w × 320h.
    const int CELL_W = 108, CELL_H = 118, GAP = 6;
    const int origin_x = (360 - (3 * CELL_W + 2 * GAP)) / 2;
    const int origin_y = 44;

    for (uint8_t i = 0; i < PROGRAMA_SLOTS; ++i) {
        int row = i / 3;
        int col = i % 3;
        int x = origin_x + col * (CELL_W + GAP);
        int y = origin_y + row * (CELL_H + GAP);

        programa_t p_slot;
        bool exists = (programa_load(i, &p_slot) == ESP_OK) && p_slot.used;

        lv_obj_t *b = lv_btn_create(p);
        lv_obj_set_size(b, CELL_W, CELL_H);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, exists ? UI_COL_GREEN : UI_COL_GREY_BTN, 0);
        lv_obj_add_event_cb(b, slot_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        char hdr[12];
        snprintf(hdr, sizeof(hdr), "PROG %u", (unsigned)(i + 1));
        lv_obj_t *h = lv_label_create(b);
        lv_label_set_text(h, hdr);
        lv_obj_set_style_text_font(h, ui_font_lg_bold(), 0);   // Bold 18
        lv_obj_set_style_text_color(h, UI_COL_WHITE, 0);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t *n = lv_label_create(b);
        if (exists) {
            lv_label_set_text(n, p_slot.nombre);
            lv_obj_set_style_text_color(n, UI_COL_WHITE, 0);
        } else {
            lv_label_set_text(n, "vacio");
            lv_obj_set_style_text_color(n, lv_color_hex(0xDDDDDD), 0);
        }
        lv_obj_set_style_text_font(n, ui_font_sm_bold(), 0);   // Bold 12
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 36);

        if (exists) {
            uint32_t total = p_slot.etapa_duration_s[0]
                           + p_slot.etapa_duration_s[1]
                           + p_slot.etapa_duration_s[2];
            char buf[24];
            snprintf(buf, sizeof(buf), "%02lu:%02lu",
                     (unsigned long)(total / 3600),
                     (unsigned long)((total / 60) % 60));
            lv_obj_t *t = lv_label_create(b);
            lv_label_set_text(t, buf);
            lv_obj_set_style_text_font(t, ui_font_md_bold(), 0);  // Bold 14
            lv_obj_set_style_text_color(t, UI_COL_YELLOW, 0);
            lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -6);
        }
    }

    // Modal de elección Editar/Iniciar. Se construye una sola vez; se muestra
    // u oculta según el slot tocado.
    build_modal(scr);
}
