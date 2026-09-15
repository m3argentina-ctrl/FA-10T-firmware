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
    float    temperature_c;       // °C = PROMEDIO de las sondas de AIRE válidas → PID
    float    max_temperature_c;   // °C = la sonda de AIRE más caliente → seguridad (OVERTEMP aire)
    bool     fault;               // true solo si fallan TODAS las sondas de aire
    uint8_t  fault_status;        // ds18b20_fault_t bitmask
    int      sensor_count;        // sondas detectadas en el bus (multidrop)
    bool     heater_assigned;     // hay una ROM asignada como sonda de resistencias
    bool     heater_valid;        // esa sonda está en el bus y su última lectura es válida
    float    heater_temperature_c;
} ds18b20_reading_t;

// Estado de cada sonda del bus (pantalla SONDAS y /api/diag).
typedef struct {
    uint64_t rom;
    float    temperature_c;
    bool     valid;
    bool     is_heater;
} ds18b20_probe_t;

// Inicializa el bus 1-Wire (GPIO PIN_ONEWIRE), escanea las sondas y fija la
// resolución. Las sondas nuevas se detectan sólo al arrancar.
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

// Sonda de resistencias: se identifica por ROM y NO entra al promedio del PID.
// 0 = todas las sondas son de aire. Se puede llamar antes de ds18b20_bus_init().
void     ds18b20_bus_set_heater_rom(uint64_t rom);
uint64_t ds18b20_bus_get_heater_rom(void);
int      ds18b20_bus_get_probes(ds18b20_probe_t *out, int max);   // devuelve cuántas copió

#ifdef __cplusplus
}
#endif
