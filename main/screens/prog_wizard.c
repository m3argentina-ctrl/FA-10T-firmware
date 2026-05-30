#include "prog_wizard.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "ui_common.h"

static const char *TAG = "wizard";

static prog_wizard_state_t s_state;

prog_wizard_state_t *prog_wizard_state(void) { return &s_state; }

static void load_or_default(uint8_t slot)
{
    if (programa_load(slot, &s_state.recipe) != ESP_OK || !s_state.recipe.used) {
        // Slot vacío en NVS → defaults razonables.
        memset(&s_state.recipe, 0, sizeof(s_state.recipe));
        snprintf(s_state.recipe.nombre, PROG_NAME_MAX, "PROG %u", (unsigned)(slot + 1));
        s_state.recipe.etapa_sp[0]         = 50.0f;
        s_state.recipe.etapa_sp[1]         = 60.0f;
        s_state.recipe.etapa_sp[2]         = 70.0f;
        s_state.recipe.etapa_duration_s[0] = 30 * 60;
        s_state.recipe.etapa_duration_s[1] = 30 * 60;
        s_state.recipe.etapa_duration_s[2] = 30 * 60;
        s_state.recipe.used                = false;
    }
}

void prog_wizard_enter(uint8_t slot)
{
    if (slot >= PROGRAMA_SLOTS) return;
    s_state.slot = slot;
    load_or_default(slot);
    s_state.current_stage = 0;
    ESP_LOGI(TAG, "enter slot=%u name='%s'", slot, s_state.recipe.nombre);
    prog_wizard_goto_stage(0);
}

void prog_wizard_goto_stage(uint8_t stage)
{
    if (stage >= PROG_STAGE_COUNT) return;
    s_state.current_stage = stage;
    // Limpiar y reconstruir la pantalla genérica de etapa.
    lv_obj_t *scr = ui_get_screen(UI_SCREEN_PROG_STAGE);
    if (scr) {
        lv_obj_clean(scr);
        screen_prog_stage_build(scr);
    }
    ui_show_screen(UI_SCREEN_PROG_STAGE);
}

void prog_wizard_show_resumen(void)
{
    lv_obj_t *scr = ui_get_screen(UI_SCREEN_PROG_RESUMEN);
    if (scr) {
        lv_obj_clean(scr);
        screen_prog_resumen_build(scr);
    }
    ui_show_screen(UI_SCREEN_PROG_RESUMEN);
}

static void persist_current(void)
{
    s_state.recipe.used = true;
    ESP_LOGI(TAG, "persist slot=%u name='%s' stages=[%.1f/%lus,%.1f/%lus,%.1f/%lus]",
             s_state.slot, s_state.recipe.nombre,
             s_state.recipe.etapa_sp[0], (unsigned long)s_state.recipe.etapa_duration_s[0],
             s_state.recipe.etapa_sp[1], (unsigned long)s_state.recipe.etapa_duration_s[1],
             s_state.recipe.etapa_sp[2], (unsigned long)s_state.recipe.etapa_duration_s[2]);
    esp_err_t err = programa_save(s_state.slot, &s_state.recipe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "programa_save slot=%u err=%s",
                 s_state.slot, esp_err_to_name(err));
    }
}

// La pantalla de selección se construye una sola vez en ui_common_init y
// queda con un snapshot del estado de NVS de ese momento. Después de grabar
// debemos limpiar y reconstruir para que la nueva fila se refleje.
static void refresh_select_screen(void)
{
    lv_obj_t *scr = ui_get_screen(UI_SCREEN_PROG_PROGRAMAS);
    if (scr) {
        lv_obj_clean(scr);
        screen_prog_select_build(scr);
    }
}

void prog_wizard_save_and_exit(void)
{
    persist_current();
    refresh_select_screen();
    ui_show_screen(UI_SCREEN_PROG_PROGRAMAS);
}

void prog_wizard_start_session(bool grabar_primero)
{
    if (grabar_primero) persist_current();
    if (programa_start_session(&s_state.recipe) == ESP_OK) {
        ui_show_screen(UI_SCREEN_FUNC_PROGRAMAS);
    }
}

void prog_wizard_start_slot(uint8_t slot)
{
    if (slot >= PROGRAMA_SLOTS) return;
    programa_t p;
    if (programa_load(slot, &p) != ESP_OK || !p.used) {
        ESP_LOGW(TAG, "start_slot %u: not used / load failed", slot);
        return;
    }
    s_state.slot   = slot;
    s_state.recipe = p;
    s_state.current_stage = 0;
    if (programa_start_session(&p) == ESP_OK) {
        ui_show_screen(UI_SCREEN_FUNC_PROGRAMAS);
    }
}
