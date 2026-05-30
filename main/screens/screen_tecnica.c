#include "ui_common.h"
#include "ui_keyboard.h"
#include "wifi_config.h"
#include "wifi_manager.h"
#include "app_state.h"
#include "telemetry.h"
#include "nvs_config.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ui_tecnica";

static bool       s_authenticated;
static lv_obj_t  *s_pin_modal;
static lv_obj_t  *s_pin_ta;

static lv_obj_t  *s_owner_scr;
static lv_obj_t  *s_box_hrs, *s_box_cyc, *s_box_svc, *s_box_fan;
static lv_obj_t  *s_box_pid, *s_box_curr, *s_box_tmax, *s_box_sess;
static lv_obj_t  *s_event_lbls[3];
static lv_obj_t  *s_model_ta;
static lv_obj_t  *s_serie_ta;
static lv_obj_t  *s_wifi_btn;
static lv_obj_t  *s_wifi_btn_lbl;
static lv_obj_t  *s_bright_bar;
static lv_obj_t  *s_bright_val;

// --- Telemetry refresh ------------------------------------------------------
static void update_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != s_owner_scr) return;

    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);
    char buf[40];

    snprintf(buf, sizeof(buf), "%luh", (unsigned long)(snap.hours_total_s / 3600));
    lv_label_set_text(s_box_hrs, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)snap.ssr_cycles_drv);
    lv_label_set_text(s_box_cyc, buf);

    uint32_t svc_h = snap.hours_service_s / 3600;
    snprintf(buf, sizeof(buf), "%s%luh", svc_h > 450 ? LV_SYMBOL_WARNING " " : "",
             (unsigned long)svc_h);
    lv_label_set_text(s_box_svc, buf);
    lv_obj_set_style_text_color(s_box_svc, svc_h > 450 ? UI_COL_YELLOW : UI_COL_WHITE, 0);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)snap.fan_fault_count);
    lv_label_set_text(s_box_fan, buf);

    fa10t_config_t cfg;
    app_state_copy_config(&cfg);
    snprintf(buf, sizeof(buf), "%.1f/%.2f", cfg.kp, cfg.ki);
    lv_label_set_text(s_box_pid, buf);

    app_state_lock();
    float fan_nom = app_state_get()->fan_nominal;
    app_state_unlock();
    snprintf(buf, sizeof(buf), "%.3f A", fan_nom);
    lv_label_set_text(s_box_curr, buf);

    snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C", snap.t_max_historica);
    lv_label_set_text(s_box_tmax, buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)snap.sessions_total);
    lv_label_set_text(s_box_sess, buf);

    // Event log
    telem_event_t evs[3];
    int n = 3;
    telemetry_get_events(evs, &n);
    for (int i = 0; i < 3; ++i) {
        if (i < n) {
            const char *prefix = "";
            lv_color_t col = UI_COL_WHITE;
            switch (evs[i].kind) {
                case TELEM_EVT_BOOT:
                case TELEM_EVT_SESSION_START:
                    prefix = "+ "; col = UI_COL_GREEN; break;
                case TELEM_EVT_FAULT:
                    prefix = "! "; col = UI_COL_RED; break;
                case TELEM_EVT_POWER_FAIL:
                    prefix = "~ "; col = UI_COL_ORANGE; break;
                default: break;
            }
            snprintf(buf, sizeof(buf), "%s%lus  %s",
                     prefix, (unsigned long)evs[i].timestamp_uptime_s, evs[i].msg);
            lv_obj_set_style_text_color(s_event_lbls[i], col, 0);
            lv_label_set_text(s_event_lbls[i], buf);
        } else {
            lv_label_set_text(s_event_lbls[i], "");
        }
    }

    // Estado WiFi: el botón se pone verde cuando hay conexión, gris si no.
    if (s_wifi_btn) {
        bool conn = wifi_manager_is_connected();
        lv_obj_set_style_bg_color(s_wifi_btn, conn ? UI_COL_GREEN : UI_COL_GREY_BTN, 0);
        lv_label_set_text(s_wifi_btn_lbl, conn ? "WIFI OK" : "WIFI");
    }
}

static void wifi_btn_cb(lv_event_t *e)
{
    (void)e;
    wifi_config_show();
}

// --- Brillo de pantalla (PWM del backlight) ---------------------------------
// Se regula con botones − / + en pasos de 10 % (en vez de un slider, que sobre
// el FT6336 + el press-latch de display.c era incómodo de arrastrar con
// precisión). Cada toque es un tap discreto: aplica el brillo, mueve la barra
// indicadora y persiste a NVS (son pocos writes por sesión, sin riesgo de wear).
#define BRIGHT_STEP_PCT  10

static void bright_step(int delta)
{
    int v = (int)display_get_backlight() + delta;
    if (v < DISPLAY_BL_PCT_MIN) v = DISPLAY_BL_PCT_MIN;
    if (v > 100)                v = 100;
    display_set_backlight((uint8_t)v);
    display_backlight_save();
    uint8_t cur = display_get_backlight();   // valor real aplicado (clampeado)
    lv_bar_set_value(s_bright_bar, cur, LV_ANIM_OFF);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", cur);
    lv_label_set_text(s_bright_val, b);
}

static void bright_minus_cb(lv_event_t *e) { (void)e; bright_step(-BRIGHT_STEP_PCT); }
static void bright_plus_cb(lv_event_t *e)  { (void)e; bright_step(+BRIGHT_STEP_PCT); }

// --- PIN modal --------------------------------------------------------------
static void pin_key_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_btnmatrix_get_btn_text(btn, lv_btnmatrix_get_selected_btn(btn));
    if (!txt) return;
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        const char *cur = lv_textarea_get_text(s_pin_ta);
        size_t l = strlen(cur);
        if (l > 0) {
            char tmp[PIN_LEN_MAX];
            size_t keep = l - 1;
            if (keep >= sizeof(tmp)) keep = sizeof(tmp) - 1;
            memcpy(tmp, cur, keep);
            tmp[keep] = '\0';
            lv_textarea_set_text(s_pin_ta, tmp);
        }
    } else if (strcmp(txt, "OK") == 0) {
        app_state_lock();
        bool ok = (strcmp(lv_textarea_get_text(s_pin_ta), app_state_get()->pin_servicio) == 0);
        app_state_unlock();
        if (ok) {
            s_authenticated = true;
            lv_obj_add_flag(s_pin_modal, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_textarea_set_text(s_pin_ta, "");
        }
    } else if (strcmp(txt, "CANCELAR") == 0) {
        // Salir sin autenticarse: limpiar el PIN tipeado y volver a INICIO.
        lv_textarea_set_text(s_pin_ta, "");
        lv_obj_add_flag(s_pin_modal, LV_OBJ_FLAG_HIDDEN);
        ui_show_screen(UI_SCREEN_INICIO);
    } else {
        lv_textarea_add_text(s_pin_ta, txt);
    }
}

static void build_pin_modal(lv_obj_t *parent)
{
    // Overlay full-screen oscuro.
    s_pin_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(s_pin_modal);
    lv_obj_set_size(s_pin_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_pin_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pin_modal, LV_OPA_COVER, 0);
    lv_obj_align(s_pin_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_pin_modal, LV_OBJ_FLAG_SCROLLABLE);

    // Layout: title (top), textarea destacado, separador, btnmatrix abajo.
    lv_obj_t *card = lv_obj_create(s_pin_modal);
    lv_obj_set_size(card, 320, 300);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "INGRESE PIN");
    lv_obj_set_style_text_font(t, ui_font_lg(), 0);
    lv_obj_set_style_text_color(t, UI_COL_BLUE, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    // Textarea bien grande y destacado: borde azul, fondo oscuro, texto grande.
    s_pin_ta = lv_textarea_create(card);
    lv_obj_set_size(s_pin_ta, 280, 44);
    lv_obj_align(s_pin_ta, LV_ALIGN_TOP_MID, 0, 30);
    lv_textarea_set_one_line(s_pin_ta, true);
    lv_textarea_set_password_mode(s_pin_ta, true);
    lv_textarea_set_max_length(s_pin_ta, 4);
    lv_obj_set_style_text_font(s_pin_ta, ui_font_xl(), 0);
    lv_obj_set_style_text_color(s_pin_ta, UI_COL_WHITE, 0);
    lv_obj_set_style_text_align(s_pin_ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_pin_ta, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(s_pin_ta, UI_COL_BLUE, 0);
    lv_obj_set_style_border_width(s_pin_ta, 2, 0);

    static const char *map[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        LV_SYMBOL_BACKSPACE, "0", "OK", "\n",
        "CANCELAR",
        ""
    };
    lv_obj_t *bm = lv_btnmatrix_create(card);
    lv_obj_set_size(bm, 290, 188);
    lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_btnmatrix_set_map(bm, map);
    lv_obj_set_style_text_font(bm, ui_font_lg(), 0);
    lv_obj_set_style_pad_all(bm, 2, 0);
    lv_obj_set_style_pad_gap(bm, 3, 0);
    lv_obj_add_event_cb(bm, pin_key_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// TODO fase 5 (logo): reemplazar los placeholders "LOGO BIO ORIGEN" en
//   screen_splash.c y screen_inicio.c + el círculo gris en ui_common.c
//   ui_left_panel_attach() por imágenes reales (lv_img + LV_IMG_DECLARE).
//   Requiere PNG → array C con la herramienta lvgl.io/tools/imageconverter.
//
// TODO fase 5 (audio): driver del codec ES8311 (I2C 0x18) + I2S DMA + habilitar
//   PA_CTRL (EXIO7 del TCA9554) + función audio_beep(freq, ms) llamada desde
//   los handlers de botón (pin_key_cb, ui_keyboard bm_event_cb, etc.).

static void on_screen_load_cb(lv_event_t *e)
{
    (void)e;
    if (!s_authenticated) lv_obj_clear_flag(s_pin_modal, LV_OBJ_FLAG_HIDDEN);
    else                  lv_obj_add_flag(s_pin_modal, LV_OBJ_FLAG_HIDDEN);
}

// --- Action buttons ---------------------------------------------------------
static void autotune_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW("autotune", "TODO: Ziegler-Nichols relay-feedback autotune");
    telemetry_log_event(TELEM_EVT_SERVICE, "autotune requested (stub)");
}

static void calibrar_cb(lv_event_t *e)
{
    (void)e;
    fa10t_config_t cfg;
    nvs_config_load(&cfg);
    cfg.cal_offset += 0.1f;
    nvs_config_save(&cfg);
    app_state_set_config(&cfg);
    ESP_LOGI("calibrar", "cal_offset → %+.2f °C", cfg.cal_offset);
}

static void reset_service_cb(lv_event_t *e)
{
    (void)e;
    telemetry_reset_service();
}

// --- Cambiar PIN: dos teclados secuenciales (nuevo + confirmar) ---
static char s_pin_pending[PIN_LEN_MAX];

static bool pin_is_valid(const char *p)
{
    size_t l = strlen(p);
    if (l < 1 || l > PIN_LEN_MAX - 1) return false;
    for (size_t i = 0; i < l; ++i) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

static void pin_confirm_done(const char *new_text, void *user_data)
{
    (void)user_data;
    if (!pin_is_valid(new_text) || strcmp(new_text, s_pin_pending) != 0) {
        ESP_LOGW(TAG, "PIN mismatch or invalid — discarded");
        return;
    }
    app_state_save_pin(new_text);
    ESP_LOGI(TAG, "PIN cambiado correctamente");
}

static void pin_new_done(const char *new_text, void *user_data)
{
    (void)user_data;
    if (!pin_is_valid(new_text)) {
        ESP_LOGW(TAG, "PIN nuevo inválido (debe ser 1-4 dígitos)");
        return;
    }
    snprintf(s_pin_pending, sizeof(s_pin_pending), "%s", new_text);
    // Pedir confirmación.
    ui_keyboard_open("", PIN_LEN_MAX, "CONFIRMAR PIN NUEVO",
                      UI_KB_MODE_NUMERIC, pin_confirm_done, NULL);
}

static void cambiar_pin_cb(lv_event_t *e)
{
    (void)e;
    s_pin_pending[0] = '\0';
    ui_keyboard_open("", PIN_LEN_MAX, "NUEVO PIN (4 dígitos)",
                      UI_KB_MODE_NUMERIC, pin_new_done, NULL);
}

// --- Edición del modelo via teclado modal ---
static void model_kb_done(const char *new_text, void *user_data)
{
    (void)user_data;
    if (!new_text || new_text[0] == '\0') return;
    app_state_save_modelo(new_text);
    lv_textarea_set_text(s_model_ta, new_text);
}

static void model_edit_cb(lv_event_t *e)
{
    (void)e;
    ui_keyboard_open(lv_textarea_get_text(s_model_ta),
                      MODEL_NAME_MAX, "EDITAR MODELO",
                      UI_KB_MODE_TEXT, model_kb_done, NULL);
}

// --- Edición del número de serie via teclado modal ---
static void serie_kb_done(const char *new_text, void *user_data)
{
    (void)user_data;
    if (!new_text) return;
    app_state_save_serie(new_text);
    lv_textarea_set_text(s_serie_ta, new_text);
}

static void serie_edit_cb(lv_event_t *e)
{
    (void)e;
    ui_keyboard_open(lv_textarea_get_text(s_serie_ta),
                      SERIE_NUM_MAX, "EDITAR Nº SERIE",
                      UI_KB_MODE_TEXT, serie_kb_done, NULL);
}

// --- Salir de AREA TECNICA: logout + volver a INICIO ---
static void salir_cb(lv_event_t *e)
{
    (void)e;
    s_authenticated = false;       // próximo ingreso vuelve a pedir PIN
    // Borrar el PIN tipeado: si no, al volver a entrar el modal muestra los
    // dígitos viejos (visualmente vacío por el password mode pero igual ahí
    // dentro). Limpiar evita que un "OK" rápido revalide sin retipear.
    if (s_pin_ta) lv_textarea_set_text(s_pin_ta, "");
    ui_show_screen(UI_SCREEN_INICIO);
}

// --- Layout -----------------------------------------------------------------
static lv_obj_t *small_box(lv_obj_t *parent, const char *cap, lv_color_t border,
                           int x, int y, lv_obj_t **out_val)
{
    lv_obj_t *b = ui_make_box(parent, UI_COL_PANEL_BG, border);
    // Caja agrandada de 34 → 42 px alto para que Bold 10 (caption) + Bold 14
    // (valor) no se superpongan; la pantalla tiene espacio vertical de sobra.
    lv_obj_set_size(b, 84, 42);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *c = lv_label_create(b);
    lv_label_set_text(c, cap);
    lv_obj_set_style_text_font(c, ui_font_xs_bold(), 0);   // Bold 10
    lv_obj_set_style_text_color(c, UI_COL_LABEL_GREY, 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *v = lv_label_create(b);
    lv_obj_set_style_text_font(v, ui_font_md_bold(), 0);   // Bold 14
    lv_obj_set_style_text_color(v, UI_COL_CYAN, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_label_set_text(v, "-");
    *out_val = v;
    return b;
}

void screen_tecnica_build(lv_obj_t *scr)
{
    s_owner_scr = scr;
    ui_left_panel_attach(scr, UI_SCREEN_TECNICA);
    lv_obj_t *p = ui_make_right_pane(scr);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "AREA TECNICA");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_BLUE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Botón WIFI arriba a la derecha: abre el modal de configuración y hace de
    // indicador de estado (verde = conectado, gris = sin conexión).
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

    // Row 1 (y=28..70 con cajas de 42 alto)
    small_box(p, "HRS TOTAL",   UI_COL_CYAN,   4,   28, &s_box_hrs);
    small_box(p, "CICLOS SSR",  UI_COL_CYAN,   92,  28, &s_box_cyc);
    small_box(p, "DESDE SVC",   UI_COL_YELLOW, 180, 28, &s_box_svc);
    small_box(p, "FALLA FAN",   UI_COL_RED,    268, 28, &s_box_fan);

    // Row 2 (y=74..116)
    small_box(p, "Kp / Ki",     UI_COL_GREEN,  4,   74, &s_box_pid);
    small_box(p, "I NOM",       UI_COL_GREEN,  92,  74, &s_box_curr);
    small_box(p, "T MAX HIST",  UI_COL_ORANGE, 180, 74, &s_box_tmax);
    small_box(p, "SESIONES",    UI_COL_CYAN,   268, 74, &s_box_sess);

    // Event log (y=120..162: 3 líneas de 14 px)
    for (int i = 0; i < 3; ++i) {
        s_event_lbls[i] = lv_label_create(p);
        lv_obj_set_style_text_font(s_event_lbls[i], ui_font_sm(), 0);
        lv_obj_align(s_event_lbls[i], LV_ALIGN_TOP_LEFT, 6, 120 + i * 14);
        lv_label_set_text(s_event_lbls[i], "");
    }

    // Model edit — tap en el textarea abre el teclado modal. y=164.
    lv_obj_t *ml = lv_label_create(p);
    lv_label_set_text(ml, "MODELO:");
    lv_obj_set_style_text_font(ml, ui_font_sm(), 0);
    lv_obj_set_style_text_color(ml, UI_COL_LABEL_GREY, 0);
    lv_obj_align(ml, LV_ALIGN_TOP_LEFT, 6, 168);

    s_model_ta = lv_textarea_create(p);
    lv_obj_set_size(s_model_ta, 100, 24);
    lv_obj_align(s_model_ta, LV_ALIGN_TOP_LEFT, 56, 164);
    lv_textarea_set_one_line(s_model_ta, true);
    lv_textarea_set_max_length(s_model_ta, MODEL_NAME_MAX - 1);
    lv_obj_set_style_text_font(s_model_ta, ui_font_sm(), 0);
    char buf[MODEL_NAME_MAX + 1];
    app_state_lock();
    snprintf(buf, sizeof(buf), "%s", app_state_get()->modelo);
    app_state_unlock();
    lv_textarea_set_text(s_model_ta, buf);
    lv_obj_add_event_cb(s_model_ta, model_edit_cb, LV_EVENT_CLICKED, NULL);

    // Nº SERIE edit — al lado del modelo, mismo patrón. y=164.
    lv_obj_t *sl = lv_label_create(p);
    lv_label_set_text(sl, "SERIE:");
    lv_obj_set_style_text_font(sl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(sl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 168, 168);

    s_serie_ta = lv_textarea_create(p);
    lv_obj_set_size(s_serie_ta, 108, 24);
    lv_obj_align(s_serie_ta, LV_ALIGN_TOP_LEFT, 210, 164);
    lv_textarea_set_one_line(s_serie_ta, true);
    lv_textarea_set_max_length(s_serie_ta, SERIE_NUM_MAX - 1);
    lv_obj_set_style_text_font(s_serie_ta, ui_font_sm(), 0);
    char sbuf[SERIE_NUM_MAX + 1];
    app_state_lock();
    snprintf(sbuf, sizeof(sbuf), "%s", app_state_get()->serie);
    app_state_unlock();
    lv_textarea_set_text(s_serie_ta, sbuf);
    lv_obj_add_event_cb(s_serie_ta, serie_edit_cb, LV_EVENT_CLICKED, NULL);

    // Brillo de pantalla — botones − / + en pasos de 10 % (PWM del backlight) con
    // una barra que sólo indica el nivel. Reemplaza al slider (incómodo de
    // arrastrar en este touch). Fila centrada (y≈235, centro del hueco) entre las
    // cajas de modelo/serie (terminan ~188) y los botones de acción (top ~282).
    lv_obj_t *brl = lv_label_create(p);
    lv_label_set_text(brl, "BRILLO:");
    lv_obj_set_style_text_font(brl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(brl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(brl, LV_ALIGN_TOP_LEFT, 6, 230);

    lv_obj_t *bminus = lv_btn_create(p);
    lv_obj_set_size(bminus, 44, 34);
    lv_obj_align(bminus, LV_ALIGN_TOP_LEFT, 62, 218);
    lv_obj_set_style_bg_color(bminus, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(bminus, bright_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bml = lv_label_create(bminus);
    lv_label_set_text(bml, "-");
    lv_obj_set_style_text_font(bml, ui_font_xl(), 0);
    lv_obj_set_style_text_color(bml, UI_COL_WHITE, 0);
    lv_obj_center(bml);

    s_bright_bar = lv_bar_create(p);
    lv_obj_set_size(s_bright_bar, 120, 14);
    lv_obj_align(s_bright_bar, LV_ALIGN_TOP_LEFT, 114, 228);
    lv_bar_set_range(s_bright_bar, 0, 100);
    lv_bar_set_value(s_bright_bar, display_get_backlight(), LV_ANIM_OFF);

    lv_obj_t *bplus = lv_btn_create(p);
    lv_obj_set_size(bplus, 44, 34);
    lv_obj_align(bplus, LV_ALIGN_TOP_LEFT, 242, 218);
    lv_obj_set_style_bg_color(bplus, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(bplus, bright_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bpl = lv_label_create(bplus);
    lv_label_set_text(bpl, "+");
    lv_obj_set_style_text_font(bpl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(bpl, UI_COL_WHITE, 0);
    lv_obj_center(bpl);

    s_bright_val = lv_label_create(p);
    lv_obj_set_style_text_font(s_bright_val, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_bright_val, UI_COL_CYAN, 0);
    lv_obj_align(s_bright_val, LV_ALIGN_TOP_LEFT, 296, 230);
    char bb[8];
    snprintf(bb, sizeof(bb), "%d%%", display_get_backlight());
    lv_label_set_text(s_bright_val, bb);

    // Action buttons (bottom) — 5 botones: SALIR + 4 acciones.
    static const struct { const char *txt; uint32_t col_hex; lv_event_cb_t cb; } acts[] = {
        { "SALIR",       0xD32F2F, salir_cb          },
        { "AUTOTUNE",    0x2196F3, autotune_cb       },
        { "CALIBRAR",    0xE87A20, calibrar_cb       },
        { "RESET SVC",   0x2E9E3B, reset_service_cb  },
        { "CAMBIAR PIN", 0x6A1B9A, cambiar_pin_cb    },
    };
    const int N = sizeof(acts) / sizeof(acts[0]);
    const int BTN_W = 66, GAP = 4;
    for (int i = 0; i < N; ++i) {
        lv_obj_t *b = lv_btn_create(p);
        lv_obj_set_size(b, BTN_W, 30);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 4 + i * (BTN_W + GAP), -8);
        lv_obj_set_style_bg_color(b, lv_color_hex(acts[i].col_hex), 0);
        lv_obj_add_event_cb(b, acts[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, acts[i].txt);
        lv_obj_set_style_text_font(bl, ui_font_xs(), 0);
        lv_obj_set_style_text_color(bl, UI_COL_WHITE, 0);
        lv_obj_center(bl);
    }

    build_pin_modal(scr);
    lv_obj_add_event_cb(scr, on_screen_load_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_timer_create(update_cb, 1000, NULL);
}
