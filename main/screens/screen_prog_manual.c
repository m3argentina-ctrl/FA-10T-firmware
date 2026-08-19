#include "ui_common.h"
#include "modo_manual.h"
#include "app_config.h"   // HUM_TARGET_MIN_PCT / MAX_PCT

#include <stdio.h>
#include <stdint.h>
#include <math.h>

// Auto-repeat de los spin buttons. HOLD_DELAY_MS es la espera ANTES del primer
// auto-repeat: con un valor alto, un toque corto normal (~150-250 ms) ejecuta un
// solo paso (no dos), así se puede afinar la temperatura de a 0,1 sin saltos.
// Al mantener apretado, tras esa espera el repeat pasa a HOLD_REPEAT_MS (rápido).
#define HOLD_DELAY_MS   450
#define HOLD_REPEAT_MS  90

// Manual-mode configuration is kept in module-static state until the user
// presses INICIAR, at which point we hand it to modo_manual_start().
static float    s_temp_c   = 60.0f;
static uint32_t s_time_s   = 60 * 60;   // 01:00
static float    s_hum_target = 0.0f;    // %RH objetivo (0 = OFF). Campo UI en bloque 4.

static lv_obj_t *s_temp_lbl;
static lv_obj_t *s_time_lbl;
static lv_obj_t *s_hum_lbl;

static lv_timer_t *s_hold_timer;
static int8_t      s_hold_dir;          // -1 / +1
static uint8_t     s_hold_kind;         // 0=temp, 1=time
static uint32_t    s_hold_count;        // pulses since press began

static void refresh_labels(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", s_temp_c);
    lv_label_set_text(s_temp_lbl, buf);
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(s_time_s / 3600),
             (unsigned long)((s_time_s / 60) % 60));
    lv_label_set_text(s_time_lbl, buf);
    if (s_hum_lbl) {
        if (s_hum_target <= 0.0f) {
            lv_label_set_text(s_hum_lbl, "OFF");
        } else {
            snprintf(buf, sizeof(buf), "%.0f%%", s_hum_target);
            lv_label_set_text(s_hum_lbl, buf);
        }
    }
}

static uint32_t time_step_for_pulse(uint32_t pulse)
{
    if (pulse <  5) return 60;          // 1 min
    if (pulse < 15) return 5 * 60;      // 5 min
    if (pulse < 25) return 10 * 60;     // 10 min
    return 30 * 60;                     // 30 min
}

// Paso de temperatura con aceleración (igual criterio que el de tiempo): un
// toque corto (pulse 0) sube 0,1 °C para afinar; manteniendo apretado acelera a
// 1/2/5 °C para recorrer todo el rango 20-80 °C en un par de segundos.
static float temp_step_for_pulse(uint32_t pulse)
{
    if (pulse <  5) return 0.1f;        // afinado: toque corto = 0,1 °C
    if (pulse < 15) return 1.0f;
    if (pulse < 25) return 2.0f;
    return 5.0f;                        // rápido
}

static void apply_step(int8_t dir, uint8_t kind, uint32_t pulse)
{
    if (kind == 0) {
        float step = temp_step_for_pulse(pulse);
        s_temp_c += step * dir;
        // En modo rápido caemos en grados enteros; el afinado mantiene 0,1 °C
        // (y de paso mata el drift de float al acumular pasos de 0,1).
        if (step >= 1.0f) s_temp_c = roundf(s_temp_c);
        else              s_temp_c = roundf(s_temp_c * 10.0f) / 10.0f;
        if (s_temp_c < 20.0f) s_temp_c = 20.0f;
        if (s_temp_c > 80.0f) s_temp_c = 80.0f;
    } else if (kind == 1) {
        uint32_t step = time_step_for_pulse(pulse);
        if (dir > 0) s_time_s += step;
        else if (s_time_s > step) s_time_s -= step;
        else s_time_s = 60;             // clamp ≥ 1 min

        if (s_time_s > 99u * 3600 + 59u * 60) s_time_s = 99u * 3600 + 59u * 60;
        if (s_time_s < 60)                    s_time_s = 60;
    } else {  // kind == 2: humedad objetivo (%RH). Por debajo del mínimo = OFF (0).
        if (dir > 0) {
            s_hum_target = (s_hum_target <= 0.0f) ? HUM_TARGET_MIN_PCT : s_hum_target + 1.0f;
            if (s_hum_target > HUM_TARGET_MAX_PCT) s_hum_target = HUM_TARGET_MAX_PCT;
        } else {
            s_hum_target = (s_hum_target <= HUM_TARGET_MIN_PCT) ? 0.0f : s_hum_target - 1.0f;
        }
    }
    refresh_labels();
}

static void hold_timer_cb(lv_timer_t *t)
{
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
    lv_timer_set_period(t, HOLD_REPEAT_MS);   // tras el 1er disparo, acelera
}

static void spin_press_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    s_hold_dir   = (code & 0x01) ? +1 : -1;
    s_hold_kind  = (code >> 1) & 0x03;   // 0=temp, 1=tiempo, 2=humedad
    s_hold_count = 0;
    apply_step(s_hold_dir, s_hold_kind, s_hold_count++);
    if (!s_hold_timer) s_hold_timer = lv_timer_create(hold_timer_cb, HOLD_DELAY_MS, NULL);
}

static void spin_release_cb(lv_event_t *e)
{
    (void)e;
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }
}

static lv_obj_t *make_spin_btn(lv_obj_t *parent, lv_color_t col,
                               const char *txt, intptr_t code)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 44, 44);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, ui_font_xxl(), 0);
    lv_obj_set_style_text_color(l, UI_COL_WHITE, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, spin_press_cb,   LV_EVENT_PRESSED,         (void *)code);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_RELEASED,        NULL);
    lv_obj_add_event_cb(b, spin_release_cb, LV_EVENT_PRESS_LOST,      NULL);
    return b;
}

static void iniciar_cb(lv_event_t *e)
{
    (void)e;
    if (modo_manual_start(s_temp_c, s_time_s, s_hum_target) == ESP_OK) {
        ui_show_screen(UI_SCREEN_FUNC_MANUAL);
    }
}

void screen_prog_manual_build(lv_obj_t *scr)
{
    // Si el usuario navegó con el dedo todavía apoyado sobre un spin button,
    // los callbacks RELEASED/PRESS_LOST pueden no dispararse y el timer de
    // auto-repeat quedaría vivo apuntando a labels ya destruidos. Lo matamos
    // al reconstruir la pantalla (mismo patrón que screen_prog_stage.c).
    if (s_hold_timer) { lv_timer_del(s_hold_timer); s_hold_timer = NULL; }

    ui_left_panel_attach(scr, UI_SCREEN_PROG_MANUAL);
    lv_obj_t *p = ui_make_right_pane(scr);

    lv_obj_t *title = lv_label_create(p);
    lv_label_set_text(title, "PROGRAMACION \"MODO MANUAL\"");
    lv_obj_set_style_text_font(title, ui_font_lg(), 0);
    lv_obj_set_style_text_color(title, UI_COL_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // --- Temperature block ---
    // El título usa font_lg (18 px → ~22 px de alto), así que ocupa hasta y≈28.
    // La etiqueta arranca en 32 para no pisarlo (antes estaba en 24 y se
    // superponían). El bloque entero baja 8 px; abajo hay lugar de sobra.
    lv_obj_t *temp_lbl = lv_label_create(p);
    lv_label_set_text(temp_lbl, "TEMPERATURA °C");
    lv_obj_set_style_text_font(temp_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(temp_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_MID, 0, 32);

    // Code bit 0: dir (+1 if set), bits 1-2: kind (0=temp, 1=tiempo, 2=humedad)
    lv_obj_t *t_minus = make_spin_btn(p, UI_COL_TIME_BLUE, "-", 0b000);
    lv_obj_align(t_minus, LV_ALIGN_TOP_MID, -130, 52);

    s_temp_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_temp_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_temp_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *t_plus = make_spin_btn(p, UI_COL_RED, "+", 0b001);
    lv_obj_align(t_plus, LV_ALIGN_TOP_MID, 130, 52);

    // --- Time block ---
    lv_obj_t *time_lbl = lv_label_create(p);
    lv_label_set_text(time_lbl, "TIEMPO HH:MM");
    lv_obj_set_style_text_font(time_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(time_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(time_lbl, LV_ALIGN_TOP_MID, 0, 104);

    lv_obj_t *m_minus = make_spin_btn(p, UI_COL_GREEN, "-", 0b010);
    lv_obj_align(m_minus, LV_ALIGN_TOP_MID, -130, 124);

    s_time_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_time_lbl, ui_font_huge(), 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_TEMP_RED, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *m_plus = make_spin_btn(p, UI_COL_GREEN, "+", 0b011);
    lv_obj_align(m_plus, LV_ALIGN_TOP_MID, 130, 124);

    // --- Humidity-target block (auto-stop). OFF = corre por tiempo. ---
    lv_obj_t *hum_lbl = lv_label_create(p);
    lv_label_set_text(hum_lbl, "HUMEDAD OBJETIVO");
    lv_obj_set_style_text_font(hum_lbl, ui_font_md(), 0);
    lv_obj_set_style_text_color(hum_lbl, UI_COL_LABEL_GREY, 0);
    lv_obj_align(hum_lbl, LV_ALIGN_TOP_MID, 0, 184);

    lv_obj_t *h_minus = make_spin_btn(p, UI_COL_CYAN, "-", 0b100);
    lv_obj_align(h_minus, LV_ALIGN_TOP_MID, -130, 204);

    s_hum_lbl = lv_label_create(p);
    lv_obj_set_style_text_font(s_hum_lbl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(s_hum_lbl, UI_COL_CYAN, 0);
    lv_obj_align(s_hum_lbl, LV_ALIGN_TOP_MID, 0, 210);

    lv_obj_t *h_plus = make_spin_btn(p, UI_COL_CYAN, "+", 0b101);
    lv_obj_align(h_plus, LV_ALIGN_TOP_MID, 130, 204);

    refresh_labels();

    // --- INICIAR ---
    lv_obj_t *go = lv_btn_create(p);
    lv_obj_set_size(go, 260, 40);
    lv_obj_align(go, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(go, UI_COL_ORANGE, 0);
    lv_obj_add_event_cb(go, iniciar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go);
    lv_label_set_text(gl, "INICIAR");
    lv_obj_set_style_text_font(gl, ui_font_xl(), 0);
    lv_obj_set_style_text_color(gl, UI_COL_WHITE, 0);
    lv_obj_center(gl);
}
