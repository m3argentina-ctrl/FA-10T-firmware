#include "wifi_config.h"
#include "ui_common.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"

static const char *TAG = "wifi_ui";

#define WIFI_UI_SCAN_MAX  20

// --- Modal principal (lista de redes + estado) ------------------------------
static lv_obj_t   *s_modal;
static lv_obj_t   *s_status_lbl;
static lv_obj_t   *s_list;
static lv_obj_t   *s_scan_btn_lbl;
static lv_timer_t *s_timer;
static bool        s_scanning;

static wifi_scan_ap_t s_scan[WIFI_UI_SCAN_MAX];
static int            s_scan_n;
static char           s_sel_ssid[33];

// --- Overlay de contraseña (lv_keyboard) ------------------------------------
static lv_obj_t   *s_pw_overlay;
static lv_obj_t   *s_pw_ta;

// ----------------------------------------------------------------------------
static void pw_close(void)
{
    if (s_pw_overlay) { lv_obj_del(s_pw_overlay); s_pw_overlay = NULL; }
    s_pw_ta = NULL;
}

static void pw_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {            // tocó el check (OK) del teclado
        const char *pass = s_pw_ta ? lv_textarea_get_text(s_pw_ta) : "";
        ESP_LOGI(TAG, "conectando a '%s'...", s_sel_ssid);
        wifi_manager_connect(s_sel_ssid, pass);
        pw_close();
    } else if (code == LV_EVENT_CANCEL) {    // tocó la tecla cerrar-teclado
        pw_close();
    }
}

// Abre el teclado QWERTY nativo para tipear la clave de 'ssid'. Se usa
// lv_keyboard (no el ui_keyboard propio) porque soporta minúsculas y símbolos,
// imprescindibles para contraseñas WiFi sensibles a mayúsculas.
static void open_password(const char *ssid)
{
    strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));

    s_pw_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pw_overlay);
    lv_obj_set_size(s_pw_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_pw_overlay, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_pw_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_pw_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_pw_overlay);
    char tb[64];
    snprintf(tb, sizeof(tb), "Clave de: %s", ssid);
    lv_label_set_text(t, tb);
    lv_obj_set_style_text_font(t, ui_font_md(), 0);
    lv_obj_set_style_text_color(t, UI_COL_WHITE, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 8, 6);

    s_pw_ta = lv_textarea_create(s_pw_overlay);
    lv_textarea_set_one_line(s_pw_ta, true);
    lv_textarea_set_max_length(s_pw_ta, 63);       // WPA2 PSK máx 63 chars
    lv_obj_set_size(s_pw_ta, LV_HOR_RES - 16, 32);
    lv_obj_align(s_pw_ta, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_font(s_pw_ta, ui_font_md(), 0);

    lv_obj_t *kb = lv_keyboard_create(s_pw_overlay);
    lv_obj_set_size(kb, LV_HOR_RES, 184);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Fuente built-in: incluye los símbolos (shift/backspace/OK) que el teclado
    // usa en sus teclas especiales; las fuentes custom no los tienen.
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_keyboard_set_textarea(kb, s_pw_ta);
    lv_obj_add_event_cb(kb, pw_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, pw_event_cb, LV_EVENT_CANCEL, NULL);
}

// ----------------------------------------------------------------------------
static void ap_row_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= s_scan_n) return;

    if (s_scan[idx].secure) {
        open_password(s_scan[idx].ssid);
    } else {
        // Red abierta: conectar directo, sin pedir clave.
        strlcpy(s_sel_ssid, s_scan[idx].ssid, sizeof(s_sel_ssid));
        ESP_LOGI(TAG, "conectando a red abierta '%s'...", s_sel_ssid);
        wifi_manager_connect(s_sel_ssid, "");
    }
}

static void populate_list(void)
{
    lv_obj_clean(s_list);
    if (s_scan_n == 0) {
        lv_list_add_text(s_list, "No se encontraron redes");
        return;
    }
    for (int i = 0; i < s_scan_n; ++i) {
        char row[64];
        // ssid + señal + "*" si tiene clave. El SSID limpio se recupera por
        // el índice guardado en user_data (no parseando este texto).
        // %.32s acota el SSID a 32 chars: sin la precisión, GCC asume que un
        // SSID sin terminador podría correr hasta el final del array s_scan[]
        // (~699 B) y dispara -Werror=format-truncation.
        snprintf(row, sizeof(row), "%.32s   %ddBm%s",
                 s_scan[i].ssid, (int)s_scan[i].rssi,
                 s_scan[i].secure ? "  *" : "");
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, row);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, ap_row_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_text_font(btn, ui_font_sm(), 0);
    }
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (s_scanning) return;
    esp_err_t err = wifi_manager_scan_start();
    if (err == ESP_OK) {
        s_scanning = true;
        if (s_scan_btn_lbl) lv_label_set_text(s_scan_btn_lbl, "Buscando...");
    } else {
        ESP_LOGW(TAG, "scan_start falló: %s", esp_err_to_name(err));
    }
}

static void close_modal(void)
{
    pw_close();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_modal) { lv_obj_del(s_modal);   s_modal = NULL; }
    s_status_lbl = s_list = s_scan_btn_lbl = NULL;
    s_scanning = false;
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

static void update_status(void)
{
    if (!s_status_lbl) return;
    char buf[80];
    char ip[16];
    const char *ss = wifi_manager_get_ssid();
    if (!ss) ss = "";
    switch (wifi_manager_state()) {
    case WIFI_MGR_STATE_CONNECTED:
        if (wifi_manager_get_ip(ip, sizeof(ip)))
            snprintf(buf, sizeof(buf), "Conectado a '%s'   IP %s", ss, ip);
        else
            snprintf(buf, sizeof(buf), "Conectado a '%s'", ss);
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_GREEN, 0);
        break;
    case WIFI_MGR_STATE_CONNECTING:
        snprintf(buf, sizeof(buf), "Conectando a '%s'...", ss);
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_YELLOW, 0);
        break;
    case WIFI_MGR_STATE_FAILED:
        snprintf(buf, sizeof(buf), "No se pudo conectar a '%s'", ss);
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_RED, 0);
        break;
    default:
        snprintf(buf, sizeof(buf), "Sin red configurada");
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_LABEL_GREY, 0);
        break;
    }
    lv_label_set_text(s_status_lbl, buf);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    update_status();
    if (s_scanning && wifi_manager_scan_ready()) {
        s_scanning = false;
        s_scan_n = wifi_manager_scan_results(s_scan, WIFI_UI_SCAN_MAX);
        populate_list();
        if (s_scan_btn_lbl) lv_label_set_text(s_scan_btn_lbl, "BUSCAR REDES");
    }
}

// ----------------------------------------------------------------------------
void wifi_config_show(void)
{
    if (s_modal) return;

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, "CONFIGURAR WIFI");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_BLUE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 6);

    s_status_lbl = lv_label_create(s_modal);
    lv_obj_set_style_text_font(s_status_lbl, ui_font_sm(), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, 10, 32);
    lv_label_set_text(s_status_lbl, "");

    s_list = lv_list_create(s_modal);
    lv_obj_set_size(s_list, LV_HOR_RES - 20, 200);
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 10, 54);
    lv_obj_set_style_bg_color(s_list, UI_COL_BLACK, 0);

    lv_obj_t *scan_btn = lv_btn_create(s_modal);
    lv_obj_set_size(scan_btn, 160, 40);
    lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_obj_set_style_bg_color(scan_btn, UI_COL_BLUE, 0);
    lv_obj_add_event_cb(scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);
    s_scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(s_scan_btn_lbl, "BUSCAR REDES");
    lv_obj_set_style_text_font(s_scan_btn_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(s_scan_btn_lbl, UI_COL_WHITE, 0);
    lv_obj_center(s_scan_btn_lbl);

    lv_obj_t *close_btn = lv_btn_create(s_modal);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    lv_obj_set_style_bg_color(close_btn, UI_COL_RED, 0);
    lv_obj_add_event_cb(close_btn, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "CERRAR");
    lv_obj_set_style_text_font(cl, ui_font_md(), 0);
    lv_obj_set_style_text_color(cl, UI_COL_WHITE, 0);
    lv_obj_center(cl);

    lv_obj_move_foreground(s_modal);

    // Estado inicial + lanzar un escaneo automático al abrir.
    update_status();
    lv_list_add_text(s_list, "Buscando redes...");
    scan_cb(NULL);

    s_timer = lv_timer_create(tick_cb, 500, NULL);
}
