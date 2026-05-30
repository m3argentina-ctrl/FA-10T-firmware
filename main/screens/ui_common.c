#include "ui_common.h"
#include "img/img_assets.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_state.h"

static const char *TAG = "ui";

static lv_obj_t *s_screens[UI_SCREEN_COUNT];

// Font getters resolve to whichever Montserrat sizes are enabled in Kconfig.
const lv_font_t *ui_font_xs (void) { return &lv_font_montserrat_10; }
const lv_font_t *ui_font_sm (void) { return &lv_font_montserrat_12; }
const lv_font_t *ui_font_md (void) { return &lv_font_montserrat_14; }
const lv_font_t *ui_font_lg (void) { return &lv_font_montserrat_18; }
const lv_font_t *ui_font_xl (void) { return &lv_font_montserrat_22; }
const lv_font_t *ui_font_xxl(void) { return &lv_font_montserrat_28; }
const lv_font_t *ui_font_huge(void){ return &lv_font_montserrat_46; }

// Bold variants generated with lv_font_conv from Montserrat-Bold.ttf.
LV_FONT_DECLARE(font_montserrat_bold_10);
LV_FONT_DECLARE(font_montserrat_bold_12);
LV_FONT_DECLARE(font_montserrat_bold_14);
LV_FONT_DECLARE(font_montserrat_bold_18);
const lv_font_t *ui_font_xs_bold(void) { return &font_montserrat_bold_10; }
const lv_font_t *ui_font_sm_bold(void) { return &font_montserrat_bold_12; }
const lv_font_t *ui_font_md_bold(void) { return &font_montserrat_bold_14; }
const lv_font_t *ui_font_lg_bold(void) { return &font_montserrat_bold_18; }

void ui_show_screen(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT || !s_screens[id]) return;
    lv_scr_load(s_screens[id]);
}

lv_obj_t *ui_get_screen(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT) return NULL;
    return s_screens[id];
}

lv_obj_t *ui_make_right_pane(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_HOR_RES - UI_RIGHT_X, LV_VER_RES);
    lv_obj_align(p, LV_ALIGN_TOP_LEFT, UI_RIGHT_X, 0);
    lv_obj_set_style_bg_color(p, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *ui_make_box(lv_obj_t *parent, lv_color_t bg, lv_color_t border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, border, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_set_style_pad_all(o, 4, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// --- Left nav panel ---------------------------------------------------------
// color is stored as 0xRRGGBB (uint32) so the array can live in .rodata.
// lv_color_hex() runs each time we paint a row.
typedef struct {
    const char     *label;
    uint32_t        color_hex;
    ui_screen_id_t  target;
} nav_btn_t;

static void nav_btn_click_cb(lv_event_t *e)
{
    ui_screen_id_t id = (ui_screen_id_t)(uintptr_t)lv_event_get_user_data(e);
    ui_show_screen(id);
}

void ui_left_panel_attach(lv_obj_t *scr, ui_screen_id_t active)
{
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, UI_LEFT_PANEL_W, LV_VER_RES);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(panel, UI_COL_LEFT_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Header: isologo (cuadrado 64×64, fondo blanco que coincide con el panel)
    lv_obj_t *logo = lv_img_create(panel);
    lv_img_set_src(logo, &isologo_64);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 4);

    // El modelo se muestra grande en SPLASH e INICIO right pane; acá en el
    // panel izq solo va el isologo + botones para no apretar tanto.

    // Buttons. All 92×32, evenly spaced in the lower 220px of the panel.
    static const nav_btn_t btns[] = {
        { "INICIO",    0x888888, UI_SCREEN_INICIO          },
        { "MANUAL",    0xE87A20, UI_SCREEN_PROG_MANUAL     },
        { "PROGRAMAS", 0x2E9E3B, UI_SCREEN_PROG_PROGRAMAS  },
        { "TECNICA",   0x2196F3, UI_SCREEN_TECNICA         },
        { "ALARMAS",   0xD32F2F, UI_SCREEN_ALARMA          },
    };
    const int n = sizeof(btns) / sizeof(btns[0]);
    const int top_y = 88;
    const int span  = LV_VER_RES - top_y - 8;
    const int step  = span / n;

    for (int i = 0; i < n; ++i) {
        int y = top_y + i * step + (step - 32) / 2;

        // Green triangle indicator (only on the active row)
        if (btns[i].target == active) {
            lv_obj_t *tri = lv_label_create(panel);
            lv_label_set_text(tri, LV_SYMBOL_PLAY);   // right-pointing triangle
            lv_obj_set_style_text_color(tri, UI_COL_GREEN_DEEP, 0);
            lv_obj_set_style_text_font(tri, ui_font_md(), 0);
            lv_obj_align(tri, LV_ALIGN_TOP_LEFT, 2, y + 8);
        }

        lv_obj_t *b = lv_btn_create(panel);
        lv_obj_set_size(b, 92, 32);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16, y);
        lv_obj_set_style_bg_color(b, lv_color_hex(btns[i].color_hex), 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, nav_btn_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)btns[i].target);

        lv_obj_t *lbl = lv_label_create(b);
        // PROGRAMAS is the longest label — drop one size so it fits.
        const lv_font_t *fnt = (btns[i].target == UI_SCREEN_PROG_PROGRAMAS)
                                ? ui_font_sm() : ui_font_md();
        lv_obj_set_style_text_font(lbl, fnt, 0);
        lv_obj_set_style_text_color(lbl, UI_COL_WHITE, 0);
        lv_label_set_text(lbl, btns[i].label);
        lv_obj_center(lbl);
    }
}

// --- Init: build every screen once ------------------------------------------
typedef void (*screen_build_fn)(lv_obj_t *);

void ui_common_init(void)
{
    static const screen_build_fn builders[UI_SCREEN_COUNT] = {
        [UI_SCREEN_SPLASH]          = screen_splash_build,
        [UI_SCREEN_INICIO]          = screen_inicio_build,
        [UI_SCREEN_PROG_MANUAL]     = screen_prog_manual_build,
        [UI_SCREEN_FUNC_MANUAL]     = screen_func_manual_build,
        [UI_SCREEN_PROG_PROGRAMAS]  = screen_prog_select_build,
        [UI_SCREEN_PROG_STAGE]      = NULL,  // built on demand by prog_wizard
        [UI_SCREEN_PROG_RESUMEN]    = NULL,  // built on demand by prog_wizard
        [UI_SCREEN_FUNC_PROGRAMAS]  = screen_func_programas_build,
        [UI_SCREEN_TECNICA]         = screen_tecnica_build,
        [UI_SCREEN_ALARMA]          = screen_alarma_build,
    };
    for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
        s_screens[i] = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_screens[i], UI_COL_PANEL_BG, 0);
        lv_obj_clear_flag(s_screens[i], LV_OBJ_FLAG_SCROLLABLE);
        if (builders[i]) builders[i](s_screens[i]);
    }
    ESP_LOGI(TAG, "screens built");
    ui_show_screen(UI_SCREEN_SPLASH);
}
