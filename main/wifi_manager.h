#pragma once

// WiFi manager — fase 4 base. Modo STA+AP simultáneo:
//   - STA: cliente que se conecta a una red existente (credenciales en NVS).
//   - AP : punto de acceso propio para configuración inicial / fallback.
//
// El módulo entero está envuelto en #if WIFI_ENABLED — con WIFI_ENABLED=0
// estas funciones quedan como stubs no-op para no agregar peso al binario.
//
// Eventos manejados internamente:
//   WIFI_EVENT_STA_START      → llamar a esp_wifi_connect()
//   WIFI_EVENT_STA_DISCONNECTED → reintentar hasta N_RETRIES y publicar evento
//   IP_EVENT_STA_GOT_IP       → publicar IP + estado CONNECTED
//   WIFI_EVENT_AP_STACONNECTED / DISCONNECTED → log informativo
//
// La persistencia (SSID + PASS) usa namespace NVS "fa10t_wifi".

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_STATE_OFF        = 0,
    WIFI_MGR_STATE_AP_ONLY    = 1,   // build STA-only: "inicializado, sin red configurada"
    WIFI_MGR_STATE_CONNECTING = 2,   // STA intentando conectar
    WIFI_MGR_STATE_CONNECTED  = 3,   // STA conectado y con IP
    WIFI_MGR_STATE_FAILED     = 4,   // STA agotó reintentos
} wifi_mgr_state_t;

// Una red encontrada en un escaneo.
typedef struct {
    char    ssid[33];   // hasta 32 chars + '\0'
    int8_t  rssi;       // dBm (negativo; más cerca de 0 = mejor señal)
    bool    secure;     // true si la red tiene clave (authmode != OPEN)
} wifi_scan_ap_t;

// Inicializa el stack WiFi en modo STA+AP. Levanta el AP siempre (si hay
// credenciales en NVS, también intenta conectar como STA). Llamar UNA sola
// vez después de NVS init. No-op si WIFI_ENABLED=0.
esp_err_t wifi_manager_init(void);

// Guarda SSID+password en NVS y arranca conexión STA. password puede ser NULL
// o "" para redes abiertas. Si ya había una conexión activa, se desconecta y
// vuelve a conectar.
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

// Arranca el AP de configuración con el SSID dado (password = WIFI_AP_PASS_DEFAULT).
// Si el AP ya está activo, lo reconfigura.
esp_err_t wifi_manager_start_ap(const char *ssid);

// True si la STA está conectada con IP válida.
bool wifi_manager_is_connected(void);

// Estado actual de la máquina de conexión.
wifi_mgr_state_t wifi_manager_state(void);

// Copia la IP actual (STA) al buffer dado. Devuelve false si no hay IP.
bool wifi_manager_get_ip(char *out_buf, size_t out_sz);

// Devuelve el SSID al que está conectado / intentando conectarse (string
// estático en RAM del módulo). NULL si nunca se configuró.
const char *wifi_manager_get_ssid(void);

// --- Escaneo de redes (asíncrono, no bloquea la UI) -------------------------
// Lanza un escaneo. NO bloquea: el resultado se recoge después con
// wifi_manager_scan_ready()/_results(). Devuelve ESP_OK si se largó el escaneo.
// No-op (ESP_ERR_INVALID_STATE) si WiFi no está inicializado / WIFI_ENABLED=0.
esp_err_t wifi_manager_scan_start(void);

// True cuando hay resultados de un escaneo listos para leer.
bool wifi_manager_scan_ready(void);

// Copia hasta 'max' redes encontradas (ya ordenadas por señal descendente)
// en out[]. Devuelve la cantidad copiada y baja el flag de "ready".
int wifi_manager_scan_results(wifi_scan_ap_t *out, int max);

#ifdef __cplusplus
}
#endif
