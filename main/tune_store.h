#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Puesta en marcha que sobrevive al RESET TOTAL: vive en la partición NVS "factory".
// Llamar sólo desde tareas con stack en RAM interna (main, ui_task): NVS desde PSRAM = PANIC.
esp_err_t tune_store_init(void);

bool      tune_store_load_pid(float *kp, float *ki, float *kd);   // false = sin autotune
esp_err_t tune_store_save_pid(float kp, float ki, float kd);
esp_err_t tune_store_clear_pid(void);

uint64_t  tune_store_load_heater_rom(void);                       // 0 = sin sonda de resistencias
esp_err_t tune_store_save_heater_rom(uint64_t rom);               // 0 = quitar asignación
