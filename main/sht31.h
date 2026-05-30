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

#ifdef __cplusplus
}
#endif
