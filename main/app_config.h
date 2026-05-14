#pragma once

#include "driver/spi_common.h"

// --- MAX31865 / RTD ---------------------------------------------------------
#define PIN_RTD_SPI_HOST     SPI2_HOST
#define PIN_RTD_MOSI         11
#define PIN_RTD_MISO         13
#define PIN_RTD_SCLK         12
#define PIN_RTD_CS           10
#define PIN_RTD_DRDY          9

#define RTD_REF_OHMS         4300.0f   // PT1000 typical
#define RTD_NOMINAL_OHMS     1000.0f   // PT1000

// --- SSR --------------------------------------------------------------------
#define PIN_SSR              21
#define SSR_PERIOD_MS        1000
#define SSR_ACTIVE_HIGH      true

// --- TFT display ------------------------------------------------------------
#define PIN_TFT_SPI_HOST     SPI3_HOST
#define PIN_TFT_MOSI         38
#define PIN_TFT_MISO         40
#define PIN_TFT_SCLK         39
#define PIN_TFT_CS           41
#define PIN_TFT_DC           42
#define PIN_TFT_RST          45
#define PIN_TFT_BL           48
#define TFT_HRES             240
#define TFT_VRES             320
#define TFT_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)

// --- Task periods (ms) ------------------------------------------------------
#define SENSOR_TASK_PERIOD_MS    100   // 10 Hz
#define CONTROL_TASK_PERIOD_MS   200   // 5 Hz
#define UI_TASK_PERIOD_MS         20   // ~50 Hz LVGL tick handler
#define WATCHDOG_TASK_PERIOD_MS  500

// --- Task stacks / priorities ----------------------------------------------
#define SENSOR_TASK_STACK     4096
#define CONTROL_TASK_STACK    4096
#define UI_TASK_STACK         8192
#define WATCHDOG_TASK_STACK   3072

// Higher = more important. Keep safety/control above UI.
#define SENSOR_TASK_PRIO         6
#define CONTROL_TASK_PRIO        7
#define WATCHDOG_TASK_PRIO       8
#define UI_TASK_PRIO             4

// Core pinning. Keep UI off the control core.
#define SENSOR_TASK_CORE         1
#define CONTROL_TASK_CORE        1
#define WATCHDOG_TASK_CORE       0
#define UI_TASK_CORE             0

// --- Safety / runaway -------------------------------------------------------
#define SAFETY_TEMP_MAX_C          250.0f
#define SAFETY_RUNAWAY_DT_C          2.0f
#define SAFETY_RUNAWAY_WINDOW_S     90.0f
#define SAFETY_RUNAWAY_DUTY_THR      0.40f
#define SAFETY_WDT_TIMEOUT_S         5
#define SAFETY_HYSTERESIS_C          5.0f   // T must drop this far below limit before clear is allowed
#define SAFETY_RECOVERY_RAMP_S       8.0f   // soft-start ramp on duty after recovery from a trip

// --- Reset / acknowledge button --------------------------------------------
#define PIN_BTN_RESET                0      // ESP32-S3 BOOT button
#define BTN_RESET_HOLD_MS         1500      // must hold this long to confirm a clear
