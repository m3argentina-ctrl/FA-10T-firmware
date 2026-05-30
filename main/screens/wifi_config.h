#pragma once

// Modal de configuración WiFi para ÁREA TÉCNICA. Vive en lv_layer_top() (igual
// que ui_keyboard / recovery_prompt), así aparece por encima de la pantalla.
//
// Flujo (decisión Emilio 2026-05-30 — "escanear y elegir"):
//   1. Al abrir lanza un escaneo no bloqueante (wifi_manager_scan_start()).
//   2. Lista las redes encontradas con su señal (RSSI).
//   3. Al tocar una red con clave → abre lv_keyboard para la contraseña →
//      wifi_manager_connect(ssid, pass). Redes abiertas conectan directo.
//   4. Muestra el estado de conexión + IP en vivo. CERRAR para salir.

#ifdef __cplusplus
extern "C" {
#endif

// Abre el modal. No hace nada si ya está abierto.
void wifi_config_show(void);

#ifdef __cplusplus
}
#endif
