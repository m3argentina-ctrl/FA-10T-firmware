#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PT1000_FAULT_NONE      = 0,
    PT1000_FAULT_OPEN      = 1 << 0,   // V_adc near rail
    PT1000_FAULT_SHORT     = 1 << 1,   // V_adc near zero
    PT1000_FAULT_OUT_RANGE = 1 << 2,   // T outside plausible window
    PT1000_FAULT_ADC_ERR   = 1 << 3,   // ADC read returned an error
} pt1000_fault_t;

typedef struct {
    float    temperature_c;   // °C, after Callendar-Van Dusen
    float    resistance_ohms; // Rpt computed from V_adc
    float    voltage_v;       // V_adc raw
    bool     fault;
    uint8_t  fault_status;    // pt1000_fault_t bitmask
} pt1000_reading_t;

esp_err_t pt1000_adc_init(void);

// Blocking single read with internal oversampling. Returns ESP_OK even if a
// fault bit is set — caller inspects reading->fault_status. Returns
// ESP_ERR_INVALID_STATE if init was not called.
esp_err_t pt1000_adc_read(pt1000_reading_t *out);

#ifdef __cplusplus
}
#endif
