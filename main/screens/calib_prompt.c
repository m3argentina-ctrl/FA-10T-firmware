#include "calib_prompt.h"
#include "ui_common.h"
#include "app_state.h"
#include "nvs_config.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "calib_ui";

// --- Pasos del asistente ----------------------------------------------------
enum { STEP_COLD = 0, STEP_HOT = 1, STEP_DONE = 2 };

// --- Referencias / límites --------------------------------------------------
#define CAL_COLD_REF_C      0.0f      // agua con hielo (slurry, bien revuelta)
#define CAL_HOT_REF_DEF_C   100.0f    // ebullición a nivel del mar (editable)
#define CAL_HOT_REF_MIN_C   90.0f
#define CAL_HOT_REF_MAX_C   101.0f
#define CAL_HOT_REF_STEP_C  0.1f
#define CAL_MIN_SPAN_C      50.0f     // separación mínima creíble entre puntos

static lv_obj_t   *s_modal;
static lv_obj_t   *s_live_lbl;     // lectura cruda en vivo
static lv_obj_t   *s_instr_lbl;    // instrucción del paso
static lv_obj_t   *s_cap_lbl;      // valores capturados
static lv_obj_t   *s_ref_lbl;      // "Ebull: 100.0 C"
static lv_obj_t   *s_ref_row;      // contenedor [-] ref [+]
static lv_obj_t   *s_main_btn;
static lv_obj_t   *s_main_lbl;
static lv_timer_t *s_timer;

static int   s_step;
static float s_cold_raw;
static float s_hot_raw;
static float s_hot_ref;
static float s_gain;
static float s_offset;
static bool  s_active;

bool calib_prompt_active(void) { return s_active; }

// Copia la lectura cruda (pre-calibración) y el flag de falla del sensor.
static void read_sensor(float *raw_out, bool *fault_out)
{
    app_state_lock();
    const app_state_t *s = app_state_get();
    float raw = s->last_sample.raw_temperature;
    bool  flt = s->last_sample.fault;
    app_state_unlock();
    if (raw_out)   *raw_out   = raw;
    if (fault_out) *fault_out = flt;
}

static void close_modal(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_modal) { lv_obj_del(s_modal);   s_modal = NULL; }
    s_live_lbl = s_instr_lbl = s_cap_lbl = NULL;
    s_ref_lbl  = s_ref_row   = NULL;
    s_main_btn = s_main_lbl  = NULL;
    s_active   = false;
}

// Refresca textos/botón/visibilidad según el paso actual.
static void refresh_ui(void)
{
    char fno[12], hno[12], cap[112];
    if (s_step >= STEP_HOT)  snprintf(fno, sizeof(fno), "%.1f", s_cold_raw);
    else                     snprintf(fno, sizeof(fno), "--");
    if (s_step >= STEP_DONE) snprintf(hno, sizeof(hno), "%.1f", s_hot_raw);
    else                     snprintf(hno, sizeof(hno), "--");
    snprintf(cap, sizeof(cap), "Frio (0 C): %s C\nCaliente (%.1f C): %s C",
             fno, s_hot_ref, hno);
    if (s_cap_lbl) lv_label_set_text(s_cap_lbl, cap);

    if (s_ref_lbl) {
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "Ebull: %.1f C", s_hot_ref);
        lv_label_set_text(s_ref_lbl, rbuf);
    }

    if (s_step == STEP_COLD) {
        if (s_instr_lbl) lv_label_set_text(s_instr_lbl,
            "Paso 1 de 2 - Sumergi el sensor en AGUA CON HIELO (0 C).\n"
            "Espera que la lectura se estabilice y toca CAPTURAR FRIO.");
        if (s_main_lbl) lv_label_set_text(s_main_lbl, "CAPTURAR FRIO");
        lv_obj_set_style_bg_color(s_main_btn, UI_COL_BLUE, 0);
        if (s_ref_row) lv_obj_clear_flag(s_ref_row, LV_OBJ_FLAG_HIDDEN);
    } else if (s_step == STEP_HOT) {
        if (s_instr_lbl) lv_label_set_text(s_instr_lbl,
            "Paso 2 de 2 - Sumergi en AGUA HIRVIENDO. Ajusta la referencia\n"
            "segun tu altitud y toca CAPTURAR CALIENTE.");
        if (s_main_lbl) lv_label_set_text(s_main_lbl, "CAPTURAR CALIENTE");
        lv_obj_set_style_bg_color(s_main_btn, UI_COL_ORANGE, 0);
        if (s_ref_row) lv_obj_clear_flag(s_ref_row, LV_OBJ_FLAG_HIDDEN);
    } else { // STEP_DONE
        char done[140];
        snprintf(done, sizeof(done),
            "Calibracion lista.\nGanancia %.4f   Offset %+.2f C\n"
            "Revisa y toca GUARDAR.",
            s_gain, s_offset);
        if (s_instr_lbl) lv_label_set_text(s_instr_lbl, done);
        if (s_main_lbl) lv_label_set_text(s_main_lbl, "GUARDAR");
        lv_obj_set_style_bg_color(s_main_btn, UI_COL_GREEN, 0);
        if (s_ref_row) lv_obj_add_flag(s_ref_row, LV_OBJ_FLAG_HIDDEN);
    }
}

// Timer: actualiza la lectura en vivo y bloquea la captura si hay falla.
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    float raw; bool flt;
    read_sensor(&raw, &flt);
    if (s_live_lbl) {
        if (flt) {
            lv_label_set_text(s_live_lbl, "Lectura: FALLA SENSOR");
            lv_obj_set_style_text_color(s_live_lbl, UI_COL_RED, 0);
        } else {
            char b[40];
            snprintf(b, sizeof(b), "Lectura actual: %.1f C", raw);
            lv_label_set_text(s_live_lbl, b);
            lv_obj_set_style_text_color(s_live_lbl, UI_COL_CYAN, 0);
        }
    }
    if (s_main_btn && s_step != STEP_DONE) {
        if (flt) lv_obj_add_state(s_main_btn, LV_STATE_DISABLED);
        else     lv_obj_clear_state(s_main_btn, LV_STATE_DISABLED);
    }
}

static void main_cb(lv_event_t *e)
{
    (void)e;
    if (s_step == STEP_COLD) {
        float raw; bool flt;
        read_sensor(&raw, &flt);
        if (flt) return;                 // no capturar con sensor en falla
        s_cold_raw = raw;
        s_step = STEP_HOT;
        refresh_ui();
    } else if (s_step == STEP_HOT) {
        float raw; bool flt;
        read_sensor(&raw, &flt);
        if (flt) return;
        float span = raw - s_cold_raw;
        if (span < CAL_MIN_SPAN_C) {      // puntos muy juntos: dato no creíble
            if (s_instr_lbl) lv_label_set_text(s_instr_lbl,
                "Diferencia entre puntos muy chica.\n"
                "Verifica el hervor y el sensor sumergido. Toca de nuevo.");
            return;                       // se queda en STEP_HOT
        }
        s_hot_raw = raw;
        s_gain    = (s_hot_ref - CAL_COLD_REF_C) / span;
        s_offset  = CAL_COLD_REF_C - s_gain * s_cold_raw;
        s_step = STEP_DONE;
        refresh_ui();
    } else { // STEP_DONE -> GUARDAR
        fa10t_config_t cfg;
        nvs_config_load(&cfg);
        cfg.cal_gain   = s_gain;
        cfg.cal_offset = s_offset;
        nvs_config_save(&cfg);
        app_state_set_config(&cfg);
        ESP_LOGI(TAG,
            "calibracion guardada: gain=%.4f offset=%+.3f "
            "(cold_raw=%.2f hot_raw=%.2f ref=%.1f)",
            s_gain, s_offset, s_cold_raw, s_hot_raw, s_hot_ref);
        close_modal();
    }
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "calibracion cancelada por el operario");
    close_modal();
}

static void ref_minus_cb(lv_event_t *e)
{
    (void)e;
    s_hot_ref -= CAL_HOT_REF_STEP_C;
    if (s_hot_ref < CAL_HOT_REF_MIN_C) s_hot_ref = CAL_HOT_REF_MIN_C;
    refresh_ui();
}

static void ref_plus_cb(lv_event_t *e)
{
    (void)e;
    s_hot_ref += CAL_HOT_REF_STEP_C;
    if (s_hot_ref > CAL_HOT_REF_MAX_C) s_hot_ref = CAL_HOT_REF_MAX_C;
    refresh_ui();
}

void calib_prompt_show(void)
{
    if (s_active) return;
    s_active   = true;
    s_step     = STEP_COLD;
    s_cold_raw = 0.0f;
    s_hot_raw  = 0.0f;
    s_hot_ref  = CAL_HOT_REF_DEF_C;
    s_gain     = 1.0f;
    s_offset   = 0.0f;

    // Fondo opaco a pantalla completa, por encima de todo (lv_layer_top).
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  CALIBRACION 2 PUNTOS");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    s_live_lbl = lv_label_create(s_modal);
    lv_label_set_text(s_live_lbl, "Lectura actual: -- C");
    lv_obj_set_style_text_font(s_live_lbl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(s_live_lbl, UI_COL_CYAN, 0);
    lv_obj_align(s_live_lbl, LV_ALIGN_TOP_MID, 0, 34);

    s_instr_lbl = lv_label_create(s_modal);
    lv_label_set_long_mode(s_instr_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_instr_lbl, 456);
    lv_obj_set_style_text_align(s_instr_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_instr_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_instr_lbl, UI_COL_WHITE, 0);
    lv_obj_align(s_instr_lbl, LV_ALIGN_TOP_MID, 0, 70);

    s_cap_lbl = lv_label_create(s_modal);
    lv_obj_set_style_text_align(s_cap_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_cap_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_cap_lbl, UI_COL_YELLOW, 0);
    lv_obj_align(s_cap_lbl, LV_ALIGN_TOP_MID, 0, 124);

    // Fila de referencia de ebullición:  [-]   Ebull: 100.0 C   [+]
    s_ref_row = lv_obj_create(s_modal);
    lv_obj_remove_style_all(s_ref_row);
    lv_obj_set_size(s_ref_row, 300, 46);
    lv_obj_align(s_ref_row, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_clear_flag(s_ref_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *minus = lv_btn_create(s_ref_row);
    lv_obj_set_size(minus, 54, 42);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(minus, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(minus, ref_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ml = lv_label_create(minus);
    lv_label_set_text(ml, "-");
    lv_obj_set_style_text_font(ml, ui_font_xl(), 0);
    lv_obj_center(ml);

    s_ref_lbl = lv_label_create(s_ref_row);
    lv_obj_set_style_text_font(s_ref_lbl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(s_ref_lbl, UI_COL_WHITE, 0);
    lv_obj_align(s_ref_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *plus = lv_btn_create(s_ref_row);
    lv_obj_set_size(plus, 54, 42);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(plus, UI_COL_GREEN, 0);
    lv_obj_add_event_cb(plus, ref_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(plus);
    lv_label_set_text(pl, "+");
    lv_obj_set_style_text_font(pl, ui_font_xl(), 0);
    lv_obj_center(pl);

    // Botón principal (cambia según el paso) + CANCELAR.
    s_main_btn = lv_btn_create(s_modal);
    lv_obj_set_size(s_main_btn, 256, 50);
    lv_obj_align(s_main_btn, LV_ALIGN_BOTTOM_LEFT, 14, -12);
    lv_obj_add_event_cb(s_main_btn, main_cb, LV_EVENT_CLICKED, NULL);
    s_main_lbl = lv_label_create(s_main_btn);
    lv_obj_set_style_text_font(s_main_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_main_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_main_lbl);

    lv_obj_t *cb = lv_btn_create(s_modal);
    lv_obj_set_size(cb, 168, 50);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_RIGHT, -14, -12);
    lv_obj_set_style_bg_color(cb, UI_COL_RED, 0);
    lv_obj_add_event_cb(cb, cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cb);
    lv_label_set_text(cl, "CANCELAR");
    lv_obj_set_style_text_font(cl, ui_font_md(), 0);
    lv_obj_set_style_text_color(cl, UI_COL_WHITE, 0);
    lv_obj_center(cl);

    refresh_ui();
    s_timer = lv_timer_create(tick_cb, 250, NULL);
    lv_obj_move_foreground(s_modal);
    ESP_LOGI(TAG, "asistente de calibracion 2 puntos abierto");
}
