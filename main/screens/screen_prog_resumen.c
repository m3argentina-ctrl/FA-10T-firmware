// Pantalla UI_SCREEN_PROG_RESUMEN: paso final del wizard.
//   - Resumen de las 3 etapas (temp + duración + total).
//   - Campo NOMBRE editable con teclado on-screen modal.
//   - Botones: ATRÁS (vuelve a la última etapa) | GRABAR | INICIAR.

#include "ui_common.h"
#include "ui_keyboard.h"
#include "prog_wizard.h"
#include "app_config.h"   // HUM_TARGET_MIN_PCT / MAX_PCT

#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "ui_resumen";

static lv_obj_t *s_name_ta;        // textarea visible en la pantalla
static lv_obj_t *s_hum_val_lbl;    // valor del objetivo de humedad de la receta

// Auto-repeat de los botones +/- de humedad: al mantener apretado repite el
// paso (mismo criterio que los spinners de MODO MANUAL). Antes usaban solo
// LV_EVENT_CLICKED → no repetían al mantener y muchas veces ni respondían bien.
#define HOLD_DELAY_MS   450
#define HOLD_REPEAT_MS  90

static lv_timer_t *s_hold_timer;
static int8_t      s_hold_dir;

// --- Humedad objetivo de la receta (auto-stop). OFF = corre por tiempo. ---
static void refresh_hum_val(void)
{
    if (!s_hum_val_lbl) return;
    prog_wizard_state_t *st = prog_wizard_state();
    if (st->recipe.humedad_objetivo <= 0.0f) {
        lv_label_set_text(s_hum_val_lbl, "OFF");
    } else {
        char b[12];
        snprintf(b, sizeof(b), "%.0f%%", st->recipe.humedad_objetivo);
        lv_label_set_text(s_hum_val_lbl, b);
    }
}

static void hum_step(int dir)
{
    prog_wizard_state_t *st = prog_wizard_state();
    float v = st->recipe.humedad_objetivo;
    if (dir > 0) {
        v = (v <= 0.0f) ? HUM_TARGET_MIN_PCT : v + 1.0f;
        if (v > HUM_TARGET_MAX_PCT) v = HUM_TARGET_MAX_PCT;
    } else {
        v = (v <= HUM_TARGET_MIN_PCT) ? 0.0f : v - 1.0f;   // bajo el mínimo = OFF
    }
    st->recipe.humedad_objetivo = v;
    refresh_hum_val();
}

static void hold_timer_cb(lv_timer_t *t)
{
    hum_step(s_hold_dir);
    lv_timer_set_period(t, HOLD_REPEAT_MS);   // tras la 1ª repetición, ritmo rápido
}

static void hum_press_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    s_hold_dir = (code & 0x01) ? +1 : -1;
    hum_step(s_hold_dir);                       // primer paso inmediato (toque corto)
    if (!s_hold_timer) s_hold_timer = lv_timer_create(hold_timer_cb, HOLD_DELAY_MS, NULL);
}

static void hum_release_cb(lv_event_t *e)
{
    (void)e;
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }
}

// --- Sincronización entre textarea visible y buffer del wizard ---
static void sync_name_to_recipe(void)
{
    prog_wizard_state_t *st = prog_wizard_state();
    const char *txt = lv_textarea_get_text(s_name_ta);
    snprintf(st->recipe.nombre, PROG_NAME_MAX, "%s", txt);
}

// --- Teclado: usa el helper genérico ui_keyboard_open() ---
static void name_kb_done(const char *new_text, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "name updated -> '%s'", new_text);
    lv_textarea_set_text(s_name_ta, new_text);
    sync_name_to_recipe();
}

static void open_kb_cb(lv_event_t *e)
{
    (void)e;
    ui_keyboard_open(lv_textarea_get_text(s_name_ta),
                      PROG_NAME_MAX, "EDITAR NOMBRE",
                      UI_KB_MODE_TEXT, name_kb_done, NULL);
}

// --- Botones de acción ---
static void atras_cb(lv_event_t *e)
{
    (void)e;
    prog_wizard_goto_stage(PROG_STAGE_COUNT - 1);
}

static void grabar_cb(lv_event_t *e)
{
    (void)e;
    sync_name_to_recipe();
    prog_wizard_save_and_exit();
}

static void iniciar_cb(lv_event_t *e)
{
    (void)e;
    sync_name_to_recipe();
    prog_wizard_start_session(true);   // graba + arranca
}

// --- Helper para una fila del resumen ---
static void make_summary_row(lv_obj_t *parent, uint8_t i, int y,
                             const programa_t *r)
{
    char hdr[12];
    snprintf(hdr, sizeof(hdr), "ETAPA %u", (unsigned)(i + 1));
    lv_obj_t *h = lv_label_create(parent);
    lv_label_set_text(h, hdr);
    lv_obj_set_style_text_font(h, ui_font_md(), 0);
    lv_obj_set_style_text_color(h, UI_COL_YELLOW, 0);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 8, y);

    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C", r->etapa_sp[i]);
    lv_obj_t *t = lv_label_create(parent);
    lv_label_set_text(t, buf);
    lv_obj_set_style_text_font(t, ui_font_lg(), 0);
    lv_obj_set_style_text_color(t, UI_COL_TEMP_RED, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 100, y - 2);

    uint32_t s = r->etapa_duration_s[i];
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
    lv_obj_t *d = lv_label_create(parent);
    lv_label_set_text(d, buf);
    lv_obj_set_style_text_font(d, ui_font_lg(), 0);
    lv_obj_set_style_text_color(d, UI_COL_TIME_BLUE, 0);
    lv_obj_align(d, LV_ALIGN_TOP_LEFT, 220, y - 2);
}

void screen_prog_resumen_build(lv_obj_t *scr)
{
    // Si se salió con el dedo apoyado en +/-, el timer de auto-repeat podría
    // quedar vivo apuntando a labels ya destruidos: matarlo al reconstruir.
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }

    ui_left_panel_attach(scr, UI_SCREEN_PROG_PROGRAMAS);
    lv_obj_t *p = ui_make_right_pane(scr);

    prog_wizard_state_t *st = prog_wizard_state();

    char tbuf[40];
    snprintf(tbuf, sizeof(tbuf), "RESUMEN - slot %u", (unsigned)(st->slot + 1));
    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, tbuf);
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_GREEN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    make_summary_row(p, 0, 36, &st->recipe);
    make_summary_row(p, 1, 64, &st->recipe);
    make_summary_row(p, 2, 92, &st->recipe);

    uint32_t total = st->recipe.etapa_duration_s[0]
                   + st->recipe.etapa_duration_s[1]
                   + st->recipe.etapa_duration_s[2];
    char tot_buf[32];
    snprintf(tot_buf, sizeof(tot_buf), "TOTAL: %02lu:%02lu",
             (unsigned long)(total / 3600),
             (unsigned long)((total / 60) % 60));
    lv_obj_t *tot = lv_label_create(p);
    lv_label_set_text(tot, tot_buf);
    lv_obj_set_style_text_font(tot, ui_font_md(), 0);
    lv_obj_set_style_text_color(tot, UI_COL_CYAN, 0);
    lv_obj_align(tot, LV_ALIGN_TOP_LEFT, 8, 120);

    // Campo NOMBRE + botón EDITAR
    lv_obj_t *name_lbl = lv_label_create(p);
    lv_label_set_text(name_lbl, "NOMBRE:");
    lv_obj_set_style_text_font(name_lbl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(name_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 8, 152);

    s_name_ta = lv_textarea_create(p);
    lv_obj_set_size(s_name_ta, 180, 28);
    lv_obj_align(s_name_ta, LV_ALIGN_TOP_LEFT, 68, 146);
    lv_textarea_set_one_line(s_name_ta, true);
    lv_textarea_set_max_length(s_name_ta, PROG_NAME_MAX - 1);
    lv_textarea_set_text(s_name_ta, st->recipe.nombre);
    lv_obj_set_style_text_font(s_name_ta, ui_font_md(), 0);
    // Tap en el textarea abre el modal con teclado.
    lv_obj_add_event_cb(s_name_ta, open_kb_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *edit_btn = lv_btn_create(p);
    lv_obj_set_size(edit_btn, 70, 28);
    lv_obj_align(edit_btn, LV_ALIGN_TOP_LEFT, 256, 146);
    lv_obj_set_style_bg_color(edit_btn, UI_COL_BLUE, 0);
    lv_obj_add_event_cb(edit_btn, open_kb_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(edit_btn);
    lv_label_set_text(el, "EDITAR");
    lv_obj_set_style_text_font(el, ui_font_sm(), 0);
    lv_obj_set_style_text_color(el, UI_COL_WHITE, 0);
    lv_obj_center(el);

    // --- Humedad objetivo (auto-stop). OFF = la receta corre por tiempo. ---
    lv_obj_t *hum_cap = lv_label_create(p);
    lv_label_set_text(hum_cap, "HUMEDAD OBJ.");
    lv_obj_set_style_text_font(hum_cap, ui_font_sm(), 0);
    lv_obj_set_style_text_color(hum_cap, UI_COL_LABEL_GREY, 0);
    lv_obj_align(hum_cap, LV_ALIGN_TOP_LEFT, 8, 208);

    lv_obj_t *hm = lv_btn_create(p);
    lv_obj_set_size(hm, 46, 34);
    lv_obj_align(hm, LV_ALIGN_TOP_LEFT, 118, 202);
    lv_obj_set_style_bg_color(hm, UI_COL_CYAN, 0);
    lv_obj_add_event_cb(hm, hum_press_cb,   LV_EVENT_PRESSED,    (void *)0);
    lv_obj_add_event_cb(hm, hum_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(hm, hum_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *hml = lv_label_create(hm);
    lv_label_set_text(hml, "-");
    lv_obj_set_style_text_font(hml, ui_font_xl(), 0);
    lv_obj_set_style_text_color(hml, UI_COL_WHITE, 0);
    lv_obj_center(hml);

    s_hum_val_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_hum_val_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_hum_val_lbl, UI_COL_CYAN, 0);
    lv_obj_align(s_hum_val_lbl, LV_ALIGN_TOP_LEFT, 178, 208);

    lv_obj_t *hp = lv_btn_create(p);
    lv_obj_set_size(hp, 46, 34);
    lv_obj_align(hp, LV_ALIGN_TOP_LEFT, 248, 202);
    lv_obj_set_style_bg_color(hp, UI_COL_CYAN, 0);
    lv_obj_add_event_cb(hp, hum_press_cb,   LV_EVENT_PRESSED,    (void *)1);
    lv_obj_add_event_cb(hp, hum_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(hp, hum_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *hpl = lv_label_create(hp);
    lv_label_set_text(hpl, "+");
    lv_obj_set_style_text_font(hpl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(hpl, UI_COL_WHITE, 0);
    lv_obj_center(hpl);

    refresh_hum_val();

    // ATRÁS / GRABAR / INICIAR
    lv_obj_t *back = lv_btn_create(p);
    lv_obj_set_size(back, 100, 38);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(back, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(back, atras_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " ATRAS");
    lv_obj_set_style_text_font(bl, ui_font_md(), 0);
    lv_obj_set_style_text_color(bl, UI_COL_WHITE, 0);
    lv_obj_center(bl);

    lv_obj_t *grabar = lv_btn_create(p);
    lv_obj_set_size(grabar, 110, 38);
    lv_obj_align(grabar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(grabar, UI_COL_RED, 0);
    lv_obj_add_event_cb(grabar, grabar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(grabar);
    lv_label_set_text(gl, "GRABAR");
    lv_obj_set_style_text_font(gl, ui_font_md(), 0);
    lv_obj_set_style_text_color(gl, UI_COL_WHITE, 0);
    lv_obj_center(gl);

    lv_obj_t *iniciar = lv_btn_create(p);
    lv_obj_set_size(iniciar, 110, 38);
    lv_obj_align(iniciar, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(iniciar, UI_COL_ORANGE, 0);
    lv_obj_add_event_cb(iniciar, iniciar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(iniciar);
    lv_label_set_text(il, "INICIAR");
    lv_obj_set_style_text_font(il, ui_font_md(), 0);
    lv_obj_set_style_text_color(il, UI_COL_WHITE, 0);
    lv_obj_center(il);
}
