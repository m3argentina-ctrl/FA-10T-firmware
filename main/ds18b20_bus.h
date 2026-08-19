#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Falla del DS18B20 (bitmask), misma idea que pt1000_fault_t para que el
// sensor_task trate ambos igual.
typedef enum {
    DS18B20_FAULT_NONE      = 0,
    DS18B20_FAULT_NO_SENSOR = 1 << 0,   // no se detectó ninguna sonda en el bus
    DS18B20_FAULT_CRC       = 1 << 1,   // CRC de la conversión inválido
    DS18B20_FAULT_OUT_RANGE = 1 << 2,   // T fuera del rango plausible
    DS18B20_FAULT_BUS_ERR   = 1 << 3,   // error de bus 1-Wire
} ds18b20_fault_t;

typedef struct {
    float    temperature_c;     // °C = PROMEDIO de las sondas válidas (arriba/abajo) → PID
    float    max_temperature_c; // °C = la sonda MÁS CALIENTE → seguridad (OVERTEMP aire)
    bool     fault;             // true solo si fallan TODAS las sondas
    uint8_t  fault_status;      // ds18b20_fault_t bitmask
    int      sensor_count;      // sondas detectadas en el bus (multidrop)
} ds18b20_reading_t;

// Inicializa el bus 1-Wire (GPIO PIN_ONEWIRE), escanea las sondas y fija la
// resolución. La sonda de control es la primera detectada (índice 0).
esp_err_t ds18b20_bus_init(void);

// Lectura NO bloqueante. Internamente dispara la conversión y la lee ~750 ms
// después; entre medio devuelve el último valor válido. Llamar periódicamente
// (a >=1 Hz alcanza). Devuelve ESP_OK aunque haya fault (mirar fault_status);
// ESP_ERR_INVALID_STATE si no se llamó a init.
esp_err_t ds18b20_bus_read(ds18b20_reading_t *out);

// Pausa/reanuda las conversiones del bus 1-Wire.
// Necesario durante una actualización OTA: escribir en flash desactiva la caché
// y bloquea interrupciones, lo que rompe el timing de microsegundos del RMT que
// usa el 1-Wire → las lecturas fallan y dispara SENSOR_FAULT en falso.
// Mientras está en pausa se conserva la última lectura válida (sin fault).
void ds18b20_bus_set_paused(bool paused);

#ifdef __cplusplus
}
#endif
