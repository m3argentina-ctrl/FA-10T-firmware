#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// MAX31865 register addresses (read; +0x80 for write)
#define MAX31865_REG_CONFIG          0x00
#define MAX31865_REG_RTD_MSB         0x01
#define MAX31865_REG_RTD_LSB         0x02
#define MAX31865_REG_HFAULT_MSB      0x03
#define MAX31865_REG_HFAULT_LSB      0x04
#define MAX31865_REG_LFAULT_MSB      0x05
#define MAX31865_REG_LFAULT_LSB      0x06
#define MAX31865_REG_FAULT_STATUS    0x07

// CONFIG bits
#define MAX31865_CFG_VBIAS           (1 << 7)
#define MAX31865_CFG_CONV_AUTO       (1 << 6)
#define MAX31865_CFG_1SHOT           (1 << 5)
#define MAX31865_CFG_3WIRE           (1 << 4)
#define MAX31865_CFG_FAULT_CLEAR     (1 << 1)
#define MAX31865_CFG_FILTER_50HZ     (1 << 0)

// Wiring mode
typedef enum {
    MAX31865_WIRE_2 = 0,
    MAX31865_WIRE_3 = 1,
    MAX31865_WIRE_4 = 0,
} max31865_wiring_t;

// Mains filter
typedef enum {
    MAX31865_FILTER_60HZ = 0,
    MAX31865_FILTER_50HZ = 1,
} max31865_filter_t;

typedef struct {
    spi_host_device_t host;
    int gpio_cs;
    int gpio_drdy;          // -1 if unused
    int clock_speed_hz;     // typical 1-5 MHz
    float r_ref_ohms;       // reference resistor on board (e.g. 4300 for PT1000)
    float r_nominal_ohms;   // PT1000 = 1000, PT100 = 100
    max31865_wiring_t wiring;
    max31865_filter_t filter;
} max31865_config_t;

typedef struct max31865_dev_s *max31865_handle_t;

typedef struct {
    uint16_t raw_rtd;       // 15-bit ADC value
    float    resistance;    // ohms
    float    temperature;   // °C
    bool     fault;         // any fault bit set
    uint8_t  fault_status;  // raw fault register
} max31865_reading_t;

esp_err_t max31865_init(const max31865_config_t *cfg, max31865_handle_t *out_handle);
esp_err_t max31865_deinit(max31865_handle_t handle);

esp_err_t max31865_read(max31865_handle_t handle, max31865_reading_t *out);
esp_err_t max31865_clear_fault(max31865_handle_t handle);

// Set bias on/off (off in idle to reduce RTD self-heating)
esp_err_t max31865_set_bias(max31865_handle_t handle, bool enable);

// Convert RTD resistance (ohms) to temperature (°C) using Callendar-Van Dusen
float max31865_resistance_to_temperature(float r_ohms, float r0_ohms);

#ifdef __cplusplus
}
#endif
