#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRAMA_SLOTS  6     // PROG 1..6 editables desde la pantalla de selección

typedef struct {
    bool     used;
    char     nombre[PROG_NAME_MAX];
    float    etapa_sp[PROG_STAGE_COUNT];
    uint32_t etapa_duration_s[PROG_STAGE_COUNT];
} programa_t;

esp_err_t programa_init(void);

esp_err_t programa_load(uint8_t slot, programa_t *out);
esp_err_t programa_save(uint8_t slot, const programa_t *p);
esp_err_t programa_erase(uint8_t slot);

// Launch a programs-mode session from slot or from a transient in-RAM
// programa_t (slot = 0xFF). The caller may use this to "INICIAR" a freshly
// configured set of stages without saving it to NVS first.
esp_err_t programa_start_session(const programa_t *p);

// Pause / Resume / Stop semantics mirror modo_manual.
esp_err_t programa_pause(void);
esp_err_t programa_resume(void);
esp_err_t programa_stop(void);

// Called from control_task each cycle.
void      programa_tick(float dt_s);

bool      programa_active(void);

#ifdef __cplusplus
}
#endif
