#include "cloud_command.h"
#include "app_config.h"

#if CLOUD_TELEMETRY_ENABLED

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "app_state.h"
#include "modo_manual.h"
#include "programa.h"
#include "safety.h"
#include "recovery.h"
#include "autotune.h"
#include "screens/ui_common.h"

static const char *TAG = "cloud_cmd";

// --- JSON mini-parser (solo para el formato conocido del server) -------------

// Extrae el valor string de "key":"value" a partir de pos. Devuelve puntero
// al inicio del valor (dentro del JSON) y lo copia a dst (hasta dst_sz-1).
// Retorna NULL si no encuentra la clave.
static const char *json_str(const char *pos, const char *key, char *dst, size_t dst_sz)
{
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":\"", key);
    const char *p = strstr(pos, needle);
    if (!p) { dst[0] = '\0'; return NULL; }
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < dst_sz) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    return p;
}

// Extrae un número de "key":123.4 a partir de pos. Devuelve el valor o def.
static float json_num(const char *pos, const char *key, float def)
{
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *p = strstr(pos, needle);
    if (!p) return def;
    p += strlen(needle);
    while (*p == ' ') p++;
    char *end;
    float v = strtof(p, &end);
    return (end != p) ? v : def;
}

uint32_t cloud_cmd_next_push_s(const char *json, uint32_t def)
{
    if (!json) return def;
    float v = json_num(json, "next_push_s", (float)def);
    if (v < 5.0f || v > 3600.0f) return def;   // fuera de rango: ignorar
    return (uint32_t)v;
}

int cloud_cmd_parse(const char *json, cloud_cmd_t *cmds, int max_cmds)
{
    if (!json || !cmds || max_cmds <= 0) return 0;

    const char *arr = strstr(json, "\"cmds\":[");
    if (!arr) return 0;
    arr += 8; // saltar "cmds":[

    int count = 0;
    const char *p = arr;
    while (count < max_cmds) {
        // Buscar inicio del próximo objeto
        p = strchr(p, '{');
        if (!p) break;

        // Buscar fin del objeto
        const char *end = strchr(p, '}');
        if (!end) break;

        cloud_cmd_t *c = &cmds[count];
        memset(c, 0, sizeof(*c));

        json_str(p, "id", c->id, sizeof c->id);
        json_str(p, "type", c->type, sizeof c->type);

        // Payload para start_manual / start_program
        c->sp    = json_num(p, "sp", 0);
        c->dur_s = (uint32_t)json_num(p, "dur_s", 0);
        c->hum   = json_num(p, "hum", 0);
        c->slot  = (int8_t)json_num(p, "slot", -1);

        if (c->id[0] && c->type[0]) {
            ESP_LOGI(TAG, "cmd[%d]: id=%s type=%s sp=%.1f dur=%lu hum=%.1f",
                     count, c->id, c->type, c->sp,
                     (unsigned long)c->dur_s, c->hum);
            count++;
        }

        p = end + 1;
        // Si encontramos ']' estamos fuera del array
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
    }

    return count;
}

bool cloud_cmd_execute(const cloud_cmd_t *cmd)
{
    if (!cmd || !cmd->type[0]) return false;

    ESP_LOGI(TAG, "ejecutando comando: %s", cmd->type);

    if (strcmp(cmd->type, "stop") == 0) {
        autotune_cancel("DETENIDO DESDE LA WEB");
        app_state_lock();
        run_state_t rs = app_state_get()->run_state;
        float temp = app_state_get()->last_sample.temperature;
        app_state_unlock();
        if (rs == RUN_STATE_ALARM) {
            safety_request_clear(temp);
            ESP_LOGI(TAG, "stop: safety_request_clear invocado");
        }
        if (modo_manual_active())  modo_manual_stop();
        if (programa_active())     programa_stop();
        // recovery_clear() omitido: recovery_tick() detecta el stop_edge y
        // limpia NVS desde watchdog_task (SRAM interna). Llamarlo acá desde
        // cloud_task (stack en PSRAM) haría write NVS → SPI cache off → PANIC.
        ui_request_screen(UI_SCREEN_INICIO);
        return true;
    }

    if (strcmp(cmd->type, "pause") == 0) {
        if (modo_manual_active())  { modo_manual_pause(); ui_request_screen(UI_SCREEN_FUNC_MANUAL); return true; }
        if (programa_active())     { programa_pause();    ui_request_screen(UI_SCREEN_FUNC_PROGRAMAS); return true; }
        ESP_LOGW(TAG, "pause: no hay sesión activa");
        return true;
    }

    if (strcmp(cmd->type, "resume") == 0) {
        if (modo_manual_active())  { modo_manual_resume(); ui_request_screen(UI_SCREEN_FUNC_MANUAL); return true; }
        if (programa_active())     { programa_resume();    ui_request_screen(UI_SCREEN_FUNC_PROGRAMAS); return true; }
        ESP_LOGW(TAG, "resume: no hay sesión pausada");
        return true;
    }

    if (strcmp(cmd->type, "start_manual") == 0) {
        if (autotune_is_running()) {
            ESP_LOGW(TAG, "start_manual rechazado: autotune en curso");
            return false;
        }
        if (cmd->sp < 20.0f || cmd->sp > 90.0f) {
            ESP_LOGW(TAG, "start_manual: sp=%.1f fuera de rango", cmd->sp);
            return false;
        }
        if (cmd->dur_s < 60 || cmd->dur_s > 86400) {
            ESP_LOGW(TAG, "start_manual: dur=%lu fuera de rango", (unsigned long)cmd->dur_s);
            return false;
        }
        // Si hay sesión activa, pararla primero
        if (modo_manual_active()) modo_manual_stop();
        if (programa_active())    programa_stop();

        esp_err_t err = modo_manual_start(cmd->sp, cmd->dur_s, cmd->hum);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_manual falló: %s", esp_err_to_name(err));
            return false;
        }
        ui_request_screen(UI_SCREEN_FUNC_MANUAL);
        return true;
    }

    if (strcmp(cmd->type, "start_program") == 0) {
        if (autotune_is_running()) {
            ESP_LOGW(TAG, "start_program rechazado: autotune en curso");
            return false;
        }
        if (cmd->slot < 0 || cmd->slot >= PROGRAMA_SLOTS) {
            ESP_LOGW(TAG, "start_program: slot=%d fuera de rango", cmd->slot);
            return false;
        }
        programa_t p;
        if (programa_load_cached((uint8_t)cmd->slot, &p) != ESP_OK || !p.used) {
            ESP_LOGW(TAG, "start_program: slot %d vacío o error", cmd->slot);
            return false;
        }
        if (modo_manual_active()) modo_manual_stop();
        if (programa_active())    programa_stop();

        esp_err_t err = programa_start_session(&p);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_program '%s' falló: %s", p.nombre, esp_err_to_name(err));
            return false;
        }
        ESP_LOGI(TAG, "start_program: slot %d '%s' iniciado", cmd->slot, p.nombre);
        ui_request_screen(UI_SCREEN_FUNC_PROGRAMAS);
        return true;
    }

    ESP_LOGW(TAG, "tipo de comando desconocido: %s", cmd->type);
    return false;
}

#endif // CLOUD_TELEMETRY_ENABLED
