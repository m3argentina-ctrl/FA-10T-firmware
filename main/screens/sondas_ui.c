#include "sondas_ui.h"
#include "ui_common.h"
#include "app_config.h"
#include "app_state.h"
#include "autotune.h"
#include "ds18b20_bus.h"
#include "tune_store.h"

#include <stdint.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "sondas_ui";

#define ROW_Y0     86
#define ROW_H      38
#define MSG_HOLD_MS 4000

static lv_obj_t   *s_modal;
static lv_obj_t   *s_row_lbl[DS18B20_MAX_SENSORS];
static lv_obj_t   *s_row_btn[DS18B20_MAX_SENSORS];
static lv_obj_t   *s_row_btn_lbl[DS18B20_MAX_SENSORS];
static uint64_t    s_row_rom[DS18B20_MAX_SENSORS];
static lv_obj_t   *s_status_lbl;
static lv_timer_t *s_timer;
static uint32_t    s_msg_until;

// 48 bits de número de serie entre el código de familia y el CRC: se muestran los 6 dígitos bajos.
static unsigned rom_short(uint64_t rom) { return (unsigned)((rom >> 8) & 0xFFFFFFu); }

static bool heating_busy(void)
{
    app_state_lock();
    const bool busy = (app_state_get()->op_mode != OP_MODE_IDLE);
    app_state_unlock();
    return busy || autotune_is_running();
}

static void set_status(const char *txt, lv_color_t col)
{
    lv_label_set_text(s_status_lbl, txt);
    lv_obj_set_style_text_color(s_status_lbl, col, 0);
}

static void flash_msg(const char *txt, lv_color_t col)
{
    set_status(txt, col);
    s_msg_until = lv_tick_get() + MSG_HOLD_MS;
}

static void refresh(void)
{
    ds18b20_probe_t p[DS18B20_MAX_SENSORS];
    const int n = ds18b20_bus_get_probes(p, DS18B20_MAX_SENSORS);
    const uint64_t heater = ds18b20_bus_get_heater_rom();
    bool heater_found = false;
    char buf[72];

    for (int i = 0; i < DS18B20_MAX_SENSORS; ++i) {
        if (i >= n) {
            s_row_rom[i] = 0;
            lv_obj_add_flag(s_row_lbl[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_row_btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        s_row_rom[i] = p[i].rom;
        heater_found |= p[i].is_heater;
        char t[16];
        if (p[i].valid) snprintf(t, sizeof t, "%.1f \xC2\xB0""C", p[i].temperature_c);
        else            snprintf(t, sizeof t, "FALLA");
        snprintf(buf, sizeof buf, "%d    ...%06X    %s    %s", i + 1, rom_short(p[i].rom), t,
                 p[i].is_heater ? "RESISTENCIAS" : "AIRE");
        lv_label_set_text(s_row_lbl[i], buf);
        lv_obj_set_style_text_color(s_row_lbl[i], p[i].is_heater ? UI_COL_ORANGE : UI_COL_WHITE, 0);
        lv_obj_clear_flag(s_row_lbl[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_row_btn_lbl[i], p[i].is_heater ? "ASIGNADA" : "ES RESIST.");
        lv_obj_set_style_bg_color(s_row_btn[i], p[i].is_heater ? UI_COL_GREEN : UI_COL_GREY_BTN, 0);
        lv_obj_clear_flag(s_row_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    if ((int32_t)(s_msg_until - lv_tick_get()) > 0) return;   // mensaje puntual todavía visible

    if (heater != 0 && !heater_found) {
        snprintf(buf, sizeof buf, "La sonda de resistencias ...%06X NO esta conectada", rom_short(heater));
        set_status(buf, UI_COL_RED);
    } else if (n == 0) {
        set_status("No se detectan sondas en el bus", UI_COL_RED);
    } else if (heater == 0 && n >= 3) {
        set_status("Hay 3 sondas y ninguna es de resistencias: el control promedia las 3", UI_COL_YELLOW);
    } else if (heater == 0) {
        set_status("Sin sonda de resistencias asignada", UI_COL_LABEL_GREY);
    } else {
        snprintf(buf, sizeof buf, "Corte por sobretemperatura de resistencias: %d \xC2\xB0""C",
                 (int)HEATER_LIMIT_TEMP_C);
        set_status(buf, UI_COL_GREEN);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void assign_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const uint64_t rom = s_row_rom[i];
    if (rom == 0 || rom == ds18b20_bus_get_heater_rom()) return;
    if (heating_busy()) {
        flash_msg("Detene el proceso antes de cambiar las sondas", UI_COL_RED);
        return;
    }
    if (tune_store_save_heater_rom(rom) != ESP_OK) {
        flash_msg("No se pudo guardar la asignacion", UI_COL_RED);
        return;
    }
    ds18b20_bus_set_heater_rom(rom);
    ESP_LOGW(TAG, "sonda de resistencias asignada desde AREA TECNICA: ROM=0x%016llX",
             (unsigned long long)rom);
    s_msg_until = 0;
    refresh();
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    if (ds18b20_bus_get_heater_rom() == 0) return;
    if (heating_busy()) {
        flash_msg("Detene el proceso antes de cambiar las sondas", UI_COL_RED);
        return;
    }
    if (tune_store_save_heater_rom(0) != ESP_OK) {
        flash_msg("No se pudo guardar el cambio", UI_COL_RED);
        return;
    }
    ds18b20_bus_set_heater_rom(0);
    ESP_LOGW(TAG, "sonda de resistencias desasignada desde AREA TECNICA");
    s_msg_until = 0;
    refresh();
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_modal) { lv_obj_del(s_modal);   s_modal = NULL; }
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_color_t col, int w, int h,
                          lv_event_cb_t cb, void *ud, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, ui_font_sm(), 0);
    lv_obj_set_style_text_color(l, UI_COL_WHITE, 0);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

void sondas_ui_show(void)
{
    if (s_modal) return;
    s_msg_until = 0;

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(s_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, "SONDAS DE TEMPERATURA");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *ins = lv_label_create(s_modal);
    lv_label_set_long_mode(ins, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ins, 456);
    lv_obj_set_style_text_align(ins, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ins, ui_font_sm(), 0);
    lv_obj_set_style_text_color(ins, UI_COL_WHITE, 0);
    lv_label_set_text(ins, "Calenta con la mano la sonda de las resistencias, fijate cual sube y "
                           "toca ES RESIST. en su fila. Si conectaste una sonda nueva, reinicia el equipo.");
    lv_obj_align(ins, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *hdr = lv_label_create(s_modal);
    lv_label_set_text(hdr, "#    CODIGO        TEMP        USO");
    lv_obj_set_style_text_font(hdr, ui_font_xs(), 0);
    lv_obj_set_style_text_color(hdr, UI_COL_LABEL_GREY, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 14, 70);

    for (int i = 0; i < DS18B20_MAX_SENSORS; ++i) {
        const int y = ROW_Y0 + i * ROW_H;
        s_row_lbl[i] = lv_label_create(s_modal);
        lv_obj_set_style_text_font(s_row_lbl[i], ui_font_md(), 0);
        lv_obj_align(s_row_lbl[i], LV_ALIGN_TOP_LEFT, 14, y + 8);
        s_row_btn[i] = make_btn(s_modal, "ES RESIST.", UI_COL_GREY_BTN, 130, 32,
                                assign_cb, (void *)(intptr_t)i, &s_row_btn_lbl[i]);
        lv_obj_align(s_row_btn[i], LV_ALIGN_TOP_RIGHT, -14, y);
    }

    s_status_lbl = lv_label_create(s_modal);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_lbl, 456);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status_lbl, ui_font_sm(), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, ROW_Y0 + DS18B20_MAX_SENSORS * ROW_H + 2);

    lv_obj_t *clr = make_btn(s_modal, "SIN SONDA DE RESISTENCIAS", UI_COL_GREY_BTN, 250, 44,
                             clear_cb, NULL, NULL);
    lv_obj_align(clr, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lv_obj_t *cls = make_btn(s_modal, "CERRAR", UI_COL_RED, 160, 44, close_cb, NULL, NULL);
    lv_obj_align(cls, LV_ALIGN_BOTTOM_RIGHT, -14, -10);

    refresh();
    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_obj_move_foreground(s_modal);
}
