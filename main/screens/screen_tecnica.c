#include "ui_common.h"
#include "ui_keyboard.h"
#include "calib_prompt.h"
#include "app_state.h"
#include "telemetry.h"
#include "nvs_config.h"
#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ui_tecnica";

// Intervalo de mantenimiento: a las 2700 h desde el último service el cuadro
// "DESDE SVC" se pone amarillo con ⚠ para avisar que toca service. Se resetea
// con el botón RESET SVC (que pide confirmación).
#define SERVICE_INTERVAL_H  2700

static bool       s_authenticated;
static lv_obj_t  *s_pin_modal;
static lv_obj_t  *s_pin_ta;
static lv_obj_t  *s_confirm_modal;

static lv_obj_t  *s_owner_scr;
static lv_obj_t  *s_box_hrs, *s_box_cyc, *s_box_svc, *s_box_fan;
static lv_obj_t  *s_box_pid, *s_box_curr, *s_box_tmax, *s_box_sess;
static lv_obj_t  *s_event_lbls[3];
static lv_obj_t  *s_model_ta;
static lv_obj_t  *s_serie_ta;
static lv_obj_t  *s_mod_ta;
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
    bool svc_due = svc_h > SERVICE_INTERVAL_H;
    snprintf(buf, sizeof(buf), "%s%luh", svc_due ? LV_SYMBOL_WARNING " " : "",
             (unsigned long)svc_h);
    lv_label_set_text(s_box_svc, buf);
    lv_obj_set_style_text_color(s_box_svc, svc_due ? UI_COL_YELLOW : UI_COL_WHITE, 0);

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

// El botón CALIBRAR se quitó de esta pantalla (v3): la temperatura la dan
// sondas DS18B20 digitales, calibradas de fábrica, así que el asistente de
// 2 puntos (que era para el PT1000) ya no hace falta. El módulo calib_prompt
// sigue en el proyecto por si se necesita un trim en el futuro.

// RESET SVC abre un modal de confirmación (botón chico, fácil de tocar sin
// querer). El reset real se hace al confirmar; ver confirm_yes_cb / modal abajo.
static void reset_service_cb(lv_event_t *e)
{
    (void)e;
    if (s_confirm_modal) lv_obj_clear_flag(s_confirm_modal, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    telemetry_reset_service();
    ESP_LOGI(TAG, "Service reset confirmado → horas desde service = 0");
    lv_obj_add_flag(s_confirm_modal, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_no_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm_modal, LV_OBJ_FLAG_HIDDEN);
}

// Overlay semitransparente con tarjeta central: "¿Resetear service?" + CANCELAR
// / SI, RESETEAR. Se crea oculto al construir la pantalla y se muestra al tocar
// RESET SVC. Se dibuja por encima de todo (creado después del PIN modal).
static void build_confirm_modal(lv_obj_t *parent)
{
    s_confirm_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(s_confirm_modal);
    lv_obj_set_size(s_confirm_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_confirm_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_confirm_modal, LV_OPA_70, 0);
    lv_obj_align(s_confirm_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_confirm_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(s_confirm_modal);
    lv_obj_set_size(card, 300, 156);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, LV_SYMBOL_WARNING " RESET DE SERVICE");
    lv_obj_set_style_text_font(t, ui_font_lg(), 0);
    lv_obj_set_style_text_color(t, UI_COL_YELLOW, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *msg = lv_label_create(card);
    lv_label_set_text(msg, "Pone a cero las horas\ndesde el ultimo service.");
    lv_obj_set_style_text_font(msg, ui_font_sm(), 0);
    lv_obj_set_style_text_color(msg, UI_COL_WHITE, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *no = lv_btn_create(card);
    lv_obj_set_size(no, 122, 40);
    lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(no, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(no, confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(no);
    lv_label_set_text(nl, "CANCELAR");
    lv_obj_set_style_text_font(nl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(nl, UI_COL_WHITE, 0);
    lv_obj_center(nl);

    lv_obj_t *yes = lv_btn_create(card);
    lv_obj_set_size(yes, 122, 40);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(yes, lv_color_hex(0x2E9E3B), 0);
    lv_obj_add_event_cb(yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yl = lv_label_create(yes);
    lv_label_set_text(yl, "SI, RESETEAR");
    lv_obj_set_style_text_font(yl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(yl, UI_COL_WHITE, 0);
    lv_obj_center(yl);
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

// --- Edición de la cantidad de módulos (turbinas/resistencias) ---
static void mod_kb_done(const char *new_text, void *user_data)
{
    (void)user_data;
    if (!new_text || new_text[0] == '\0') return;
    int n = atoi(new_text);
    if (n < 1)           n = 1;
    if (n > MODULOS_MAX) n = MODULOS_MAX;
    app_state_save_num_modulos((uint8_t)n);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", n);
    lv_textarea_set_text(s_mod_ta, buf);
}

static void mod_edit_cb(lv_event_t *e)
{
    (void)e;
    ui_keyboard_open(lv_textarea_get_text(s_mod_ta),
                      3, "MODULOS (turbinas)",
                      UI_KB_MODE_NUMERIC, mod_kb_done, NULL);
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

// Columna editable: título (label) ARRIBA y un textarea editable ABAJO, ambos
// hijos del pane (no anidados, para que el fondo del textarea no tape el título
// como pasaba antes). El textarea ya dibuja su propio borde → parece un cuadro.
// Devuelve el textarea; cada caller le pone max_length, texto inicial y engancha
// su callback de edición (tap → teclado modal). x = borde izq de la columna.
static lv_obj_t *make_id_box(lv_obj_t *parent, const char *cap, int x, int y, int w)
{
    lv_obj_t *c = lv_label_create(parent);
    lv_label_set_text(c, cap);
    lv_obj_set_style_text_font(c, ui_font_xs_bold(), 0);   // Bold 10
    lv_obj_set_style_text_color(c, UI_COL_LABEL_GREY, 0);
    lv_obj_set_width(c, w);
    lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, 30);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x, y + 15);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_text_font(ta, ui_font_sm(), 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(ta, 4, 0);
    lv_obj_set_style_pad_bottom(ta, 4, 0);
    lv_obj_set_style_border_color(ta, UI_COL_GREY_BTN, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    return ta;
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

    // El botón WIFI se movió a la pantalla de INICIO: el cliente conecta el
    // equipo a su red desde ahí, y a AREA TECNICA (con PIN) entramos solo
    // nosotros. Ver screen_inicio.c.

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

    // --- Identidad: MODELO / SERIE / MODULOS en 3 cuadros (título arriba,
    //     textarea editable abajo). Tap en el textarea abre el teclado modal.
    const int IB_W = 114, IB_GAP = 6, IB_Y = 164;

    s_model_ta = make_id_box(p, "MODELO", 3, IB_Y, IB_W);
    lv_textarea_set_max_length(s_model_ta, MODEL_NAME_MAX - 1);
    char buf[MODEL_NAME_MAX + 1];
    app_state_lock();
    snprintf(buf, sizeof(buf), "%s", app_state_get()->modelo);
    app_state_unlock();
    lv_textarea_set_text(s_model_ta, buf);
    lv_obj_add_event_cb(s_model_ta, model_edit_cb, LV_EVENT_CLICKED, NULL);

    s_serie_ta = make_id_box(p, "SERIE", 3 + (IB_W + IB_GAP), IB_Y, IB_W);
    lv_textarea_set_max_length(s_serie_ta, SERIE_NUM_MAX - 1);
    char sbuf[SERIE_NUM_MAX + 1];
    app_state_lock();
    snprintf(sbuf, sizeof(sbuf), "%s", app_state_get()->serie);
    app_state_unlock();
    lv_textarea_set_text(s_serie_ta, sbuf);
    lv_obj_add_event_cb(s_serie_ta, serie_edit_cb, LV_EVENT_CLICKED, NULL);

    s_mod_ta = make_id_box(p, "MODULOS", 3 + 2 * (IB_W + IB_GAP), IB_Y, IB_W);
    lv_textarea_set_max_length(s_mod_ta, 2);
    char mbuf[4];
    app_state_lock();
    snprintf(mbuf, sizeof(mbuf), "%u", (unsigned)app_state_get()->num_modulos);
    app_state_unlock();
    lv_textarea_set_text(s_mod_ta, mbuf);
    lv_obj_add_event_cb(s_mod_ta, mod_edit_cb, LV_EVENT_CLICKED, NULL);

    // Brillo de pantalla — botones − / + en pasos de 10 % (PWM del backlight),
    // centrado y un poco más abajo, debajo de la fila de identidad (textareas
    // terminan ~209). La barra sólo indica el nivel (el slider era incómodo de
    // arrastrar al touch). Todo el grupo va alineado a TOP_MID del pane.
    lv_obj_t *brl = lv_label_create(p);
    lv_label_set_text(brl, "BRILLO");
    lv_obj_set_style_text_font(brl, ui_font_sm(), 0);
    lv_obj_set_style_text_color(brl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(brl, LV_ALIGN_TOP_MID, -26, 224);

    s_bright_val = lv_label_create(p);
    lv_obj_set_style_text_font(s_bright_val, ui_font_sm(), 0);
    lv_obj_set_style_text_color(s_bright_val, UI_COL_CYAN, 0);
    lv_obj_align(s_bright_val, LV_ALIGN_TOP_MID, 26, 224);
    char bb[8];
    snprintf(bb, sizeof(bb), "%d%%", display_get_backlight());
    lv_label_set_text(s_bright_val, bb);

    lv_obj_t *bminus = lv_btn_create(p);
    lv_obj_set_size(bminus, 38, 28);
    lv_obj_align(bminus, LV_ALIGN_TOP_MID, -83, 242);
    lv_obj_set_style_bg_color(bminus, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(bminus, bright_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bml = lv_label_create(bminus);
    lv_label_set_text(bml, "-");
    lv_obj_set_style_text_font(bml, ui_font_lg(), 0);
    lv_obj_set_style_text_color(bml, UI_COL_WHITE, 0);
    lv_obj_center(bml);

    s_bright_bar = lv_bar_create(p);
    lv_obj_set_size(s_bright_bar, 104, 12);
    lv_obj_align(s_bright_bar, LV_ALIGN_TOP_MID, 0, 250);
    lv_bar_set_range(s_bright_bar, 0, 100);
    lv_bar_set_value(s_bright_bar, display_get_backlight(), LV_ANIM_OFF);

    lv_obj_t *bplus = lv_btn_create(p);
    lv_obj_set_size(bplus, 38, 28);
    lv_obj_align(bplus, LV_ALIGN_TOP_MID, 83, 242);
    lv_obj_set_style_bg_color(bplus, UI_COL_GREY_BTN, 0);
    lv_obj_add_event_cb(bplus, bright_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bpl = lv_label_create(bplus);
    lv_label_set_text(bpl, "+");
    lv_obj_set_style_text_font(bpl, ui_font_lg(), 0);
    lv_obj_set_style_text_color(bpl, UI_COL_WHITE, 0);
    lv_obj_center(bpl);

    // Action buttons (bottom) — 4 botones: SALIR + 3 acciones (CALIBRAR se quitó
    // en la v3, ver nota arriba). Los 4 se reparten todo el ancho del panel:
    //   4 botones × 85 + 3 gaps × 4 = 352, con márgenes de 4 → 360 exactos.
    // Al ser más anchos entran con font sm (12 px) en vez de xs (10), más legible.
    static const struct { const char *txt; uint32_t col_hex; lv_event_cb_t cb; } acts[] = {
        { "SALIR",       0xD32F2F, salir_cb          },
        { "AUTOTUNE",    0x2196F3, autotune_cb       },
        { "RESET SVC",   0x2E9E3B, reset_service_cb  },
        { "CAMBIAR PIN", 0x6A1B9A, cambiar_pin_cb    },
    };
    const int N = sizeof(acts) / sizeof(acts[0]);
    const int BTN_W = 85, GAP = 4;
    for (int i = 0; i < N; ++i) {
        lv_obj_t *b = lv_btn_create(p);
        lv_obj_set_size(b, BTN_W, 30);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 4 + i * (BTN_W + GAP), -8);
        lv_obj_set_style_bg_color(b, lv_color_hex(acts[i].col_hex), 0);
        lv_obj_add_event_cb(b, acts[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, acts[i].txt);
        lv_obj_set_style_text_font(bl, ui_font_sm(), 0);
        lv_obj_set_style_text_color(bl, UI_COL_WHITE, 0);
        lv_obj_center(bl);
    }

    build_pin_modal(scr);
    build_confirm_modal(scr);   // overlay de confirmación del RESET SVC
    lv_obj_add_event_cb(scr, on_screen_load_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_timer_create(update_cb, 1000, NULL);
}
