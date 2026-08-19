#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Confirma la imagen actual si viene de una actualización (estado
// PENDING_VERIFY). Llamar UNA vez al arrancar, DESPUÉS de que el equipo pasó su
// autodiagnóstico (sensores + safety + tasks arriba). Si no se llama, el
// bootloader hace ROLLBACK solo a la partición anterior en el próximo reset:
// esa es justamente la red de seguridad ante una actualización fallida.
// No-op si la imagen ya estaba confirmada o si el rollback está deshabilitado.
esp_err_t ota_update_mark_valid(void);

// Registra los endpoints de actualización en el servidor web:
//   GET  /update  → página con el formulario de carga del .bin
//   POST /update  → recibe el binario, lo graba en el slot libre y reinicia
// Se llama desde web_server_start().
esp_err_t ota_update_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
