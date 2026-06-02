#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"
#include "programa.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool       valid;
    op_mode_t  op_mode;
    uint8_t    etapa_activa;
    uint32_t   session_elapsed_s;
    programa_t recipe;
    // Consumo acumulado de la sesión (por módulo). Se guarda para no perderlo si
    // se corta la luz en medio de un secado y luego se retoma.
    float      session_energy_wh;
    float      session_fan_on_s;
} recovery_snapshot_t;

esp_err_t recovery_init(void);

// True iff a non-empty snapshot exists in NVS from a previous run.
bool      recovery_has_session(recovery_snapshot_t *out);

// Persist the live session every ~RECOVERY_SAVE_INTERVAL_S while running, and
// clear the slot when the session ends cleanly.
void      recovery_tick(float dt_s);
void      recovery_clear(void);

// Hand the saved snapshot to programa_start_session() (sets elapsed forward).
esp_err_t recovery_resume_from(const recovery_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
