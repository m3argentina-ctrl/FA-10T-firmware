#include "recovery_prompt.h"
#include "ui_common.h"
#include "app_state.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "recov_ui";

// Ventana de cuenta regresiva antes de reanudar solo. 20 s da tiempo de sobra
// al operario para descartar sin trabar el equipo si no hay nadie (corte
// nocturno → el lote se recupera igual).
#define RECOVERY_COUNTDOWN_S   20

static lv_obj_t           *s_modal;
static lv_obj_t           *s_count_lbl;
static lv_timer_t         *s_timer;
static recovery_snapshot_t s_snap;
static int                 s_remaining;
static bool                s_active;

bool recovery_prompt_active(void) { return s_active; }

static void format_hhmmss(uint32_t s, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

static void close_modal(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_modal) { lv_obj_del(s_modal);   s_modal = NULL; }
    s_count_lbl = NULL;
    s_active    = false;
}

static void do_resume(void)
{
    op_mode_t mode = s_snap.op_mode;
    esp_err_t err  = recovery_resume_from(&s_snap);
    close_modal();
    if (err == ESP_OK) {
        ui_show_screen(mode == OP_MODE_PROGRAMS ? UI_SCREEN_FUNC_PROGRAMAS
                                                : UI_SCREEN_FUNC_MANUAL);
        ESP_LOGI(TAG, "sesión recuperada y reanudada");
    } else {
        ESP_LOGW(TAG, "resume falló (%s) — descartando", esp_err_to_name(err));
        recovery_clear();
        ui_show_screen(UI_SCREEN_INICIO);
    }
}

static void resume_cb(lv_event_t *e)
{
    (void)e;
    do_resume();
}

static void discard_cb(lv_event_t *e)
{
    (void)e;
    recovery_clear();
    close_modal();
    ui_show_screen(UI_SCREEN_INICIO);
    ESP_LOGI(TAG, "sesión interrumpida descartada por el operario");
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (--s_remaining <= 0) {
        ESP_LOGI(TAG, "timeout — reanudando automáticamente");
        do_resume();
        return;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "Reanudando en %d s...", s_remaining);
    if (s_count_lbl) lv_label_set_text(s_count_lbl, buf);
}

void recovery_prompt_show(const recovery_snapshot_t *snap)
{
    if (!snap || !snap->valid || s_active) return;
    s_snap      = *snap;
    s_remaining = RECOVERY_COUNTDOWN_S;
    s_active    = true;

    // Fondo opaco a pantalla completa (tapa el splash / la pantalla de abajo).
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, LV_SYMBOL_REFRESH "  PROCESO INTERRUMPIDO");
    lv_obj_set_style_text_font(title, ui_font_xl(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *q = lv_label_create(s_modal);
    lv_label_set_text(q, "Se detecto una sesion sin terminar:");
    lv_obj_set_style_text_font(q, ui_font_md(), 0);
    lv_obj_set_style_text_color(q, UI_COL_WHITE, 0);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 50);

    // Caja con los datos de la sesión.
    char buf[110], el[16];
    format_hhmmss(s_snap.session_elapsed_s, el, sizeof(el));
    lv_obj_t *info = ui_make_box(s_modal, UI_COL_BLACK, UI_COL_GREEN);
    lv_obj_set_size(info, 384, 86);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_t *info_lbl = lv_label_create(info);
    lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_lbl, 366);
    if (s_snap.op_mode == OP_MODE_PROGRAMS) {
        snprintf(buf, sizeof(buf),
                 "%s\nMODO PROGRAMA  -  ETAPA %u/%u\nTranscurrido: %s",
                 s_snap.recipe.nombre,
                 (unsigned)(s_snap.etapa_activa + 1), (unsigned)PROG_STAGE_COUNT, el);
    } else {
        snprintf(buf, sizeof(buf),
                 "%s\nMODO MANUAL  -  SP %.1f C\nTranscurrido: %s",
                 s_snap.recipe.nombre, s_snap.recipe.etapa_sp[0], el);
    }
    lv_label_set_text(info_lbl, buf);
    lv_obj_set_style_text_font(info_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(info_lbl, UI_COL_WHITE, 0);
    lv_obj_center(info_lbl);

    s_count_lbl = lv_label_create(s_modal);
    char cbuf[48];
    snprintf(cbuf, sizeof(cbuf), "Reanudando en %d s...", s_remaining);
    lv_label_set_text(s_count_lbl, cbuf);
    lv_obj_set_style_text_font(s_count_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_count_lbl, UI_COL_YELLOW, 0);
    lv_obj_align(s_count_lbl, LV_ALIGN_TOP_MID, 0, 178);

    lv_obj_t *rb = lv_btn_create(s_modal);
    lv_obj_set_size(rb, 210, 50);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_LEFT, 18, -14);
    lv_obj_set_style_bg_color(rb, UI_COL_GREEN, 0);
    lv_obj_add_event_cb(rb, resume_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, "RECUPERAR AHORA");
    lv_obj_set_style_text_font(rl, ui_font_md(), 0);
    lv_obj_set_style_text_color(rl, UI_COL_WHITE, 0);
    lv_obj_center(rl);

    lv_obj_t *db = lv_btn_create(s_modal);
    lv_obj_set_size(db, 210, 50);
    lv_obj_align(db, LV_ALIGN_BOTTOM_RIGHT, -18, -14);
    lv_obj_set_style_bg_color(db, UI_COL_RED, 0);
    lv_obj_add_event_cb(db, discard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "DESCARTAR");
    lv_obj_set_style_text_font(dl, ui_font_md(), 0);
    lv_obj_set_style_text_color(dl, UI_COL_WHITE, 0);
    lv_obj_center(dl);

    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_obj_move_foreground(s_modal);
    ESP_LOGI(TAG, "modal recovery: '%s' mode=%d etapa=%u t=%s (cuenta %d s)",
             s_snap.recipe.nombre, (int)s_snap.op_mode,
             s_snap.etapa_activa, el, s_remaining);
}
