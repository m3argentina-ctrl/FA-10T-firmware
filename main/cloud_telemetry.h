#pragma once

// Cliente de telemetría a la nube (fase 4 — F1).
//
// El equipo queda FIJO en la fábrica del cliente y empuja su estado a un
// backend propio (bioorigen-web, Next.js + Prisma) por HTTPS. El cliente lo
// mira desde su casa y Bio Origen monitorea toda la flota. Como un equipo sin
// luz/internet NO puede avisar que se cayó, el backend infiere el corte por la
// AUSENCIA de heartbeats (ver CLOUD_PUSH_INTERVAL_S).
//
// Cadencia:
//   - heartbeat cada CLOUD_PUSH_INTERVAL_S
//   - push inmediato al bootear (primera conexión) y ante cualquier cambio de
//     run_state (sobre todo la transición a RUN_STATE_ALARM)
//
// Identidad: pre-cargada de fábrica en la partición NVS dedicada "factory"
// (claves dev_id / token / cloud_url). Si no está provista, cae a los defaults
// de compilación de device_identity.h (gitignored) para banco/desarrollo. Sin
// identidad válida el módulo queda inerte (sólo loguea un aviso).
//
// Todo el módulo está envuelto en #if CLOUD_TELEMETRY_ENABLED — con el flag en
// 0 estas funciones son stubs no-op y no agregan peso al binario.

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Carga la identidad y lanza la tarea de telemetría. Idempotente. Llamar UNA
// vez, después de wifi_manager_init(). No bloquea: la tarea espera a que la STA
// tenga IP antes de empujar nada. No-op si CLOUD_TELEMETRY_ENABLED=0.
esp_err_t cloud_telemetry_start(void);

// Detiene la tarea (libera el handle). Rara vez necesario.
void cloud_telemetry_stop(void);

// True si se cargó una identidad válida (dev_id + token + cloud_url no vacíos).
bool cloud_telemetry_is_provisioned(void);

// dev_id cargado (string estático del módulo). "" si no provisto.
const char *cloud_telemetry_device_id(void);

#ifdef __cplusplus
}
#endif
