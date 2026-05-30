#pragma once

// BLE monitor — fase 4 base. Servidor GATT NimBLE que expone el estado
// operativo del equipo para que un cliente BLE genérico (nRF Connect, app
// custom, etc.) lo lea o reciba notificaciones.
//
// Servicio FA-10T (UUID 16-bit 0xFA10) con 4 características:
//   - TEMP_ACTUAL  (0xFA11)  read         float (°C)
//   - SETPOINT     (0xFA12)  read+write   float (°C)
//   - ESTADO       (0xFA13)  read+notify  string utf-8 ("FUNCIONANDO", etc.)
//   - TIEMPO_RES   (0xFA14)  read+notify  uint32 (segundos restantes)
//
// Advertising name: BLE_DEVICE_NAME_DEFAULT (definido en app_config.h).
//
// Encerrado en #if BLE_ENABLED. Con BLE_ENABLED=0 son stubs no-op.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_monitor_init(void);

// Helpers para que el firmware empuje cambios. ESTADO y TIEMPO_RES disparan
// notificación a clientes suscriptos.
void ble_monitor_publish_temperature(float t_c);
void ble_monitor_publish_setpoint(float sp_c);
void ble_monitor_publish_estado(const char *txt);
void ble_monitor_publish_tiempo_restante(uint32_t seconds);

bool ble_monitor_is_advertising(void);
bool ble_monitor_has_client(void);

#ifdef __cplusplus
}
#endif
