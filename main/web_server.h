#pragma once

// Servidor web de monitoreo (fase 4) — dashboard de SOLO LECTURA servido por el
// propio ESP32 en el puerto 80. Se accede desde un navegador en la misma red
// WiFi entrando a la IP del equipo (ej. http://192.168.0.14).
//
// Endpoints:
//   GET /             → página HTML del dashboard (embebida en flash)
//   GET /api/status   → JSON con el estado en vivo (app_state + telemetría)
//   GET /api/events   → JSON con el log de eventos reciente
//
// Todo el módulo está encerrado en #if WEB_SERVER_ENABLED — con la flag en 0
// estas funciones quedan como stubs no-op (no agregan peso al binario ni
// dependen de esp_http_server). Requiere WiFi conectado para ser alcanzable,
// pero el httpd se puede arrancar antes de tener IP (escucha en el stack LwIP).

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Arranca el servidor HTTP y registra los handlers. Idempotente (si ya está
// corriendo devuelve ESP_OK). Llamar una vez después de wifi_manager_init().
// No-op (ESP_OK) si WEB_SERVER_ENABLED=0.
esp_err_t web_server_start(void);

// Detiene el servidor HTTP si está corriendo. No-op si no arrancó.
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
