#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the I2C master bus (shared between SHT31 and PCF85063) and add
// the SHT31 device. Safe to call multiple times.
esp_err_t sht31_init(void);

// Returns the I2C bus handle so other devices on the same bus (e.g. the RTC)
// can attach. NULL if init has not run.
i2c_master_bus_handle_t sht31_shared_bus(void);

// Single-shot read with high repeatability. Blocks ~20 ms.
esp_err_t sht31_read(float *humidity_pct, float *temperature_c);

// Enciende/apaga el calentador interno del SHT31 (anti-condensación).
esp_err_t sht31_heater(bool on);

// Lectura "gestionada": corre la máquina anti-condensación (pulsa el heater si
// la RH está muy alta, ver SHT31_HEATER_* en app_config.h). Durante el pulso y
// el enfriado devuelve *valid=false (sin lectura nueva; el caller mantiene el
// último valor). En operación normal devuelve *valid=true con rh/t frescos.
// Self-timed (usa esp_timer). Llamar ~1 Hz.
esp_err_t sht31_read_managed(float *humidity_pct, float *temperature_c, bool *valid);

#ifdef __cplusplus
}
#endif
