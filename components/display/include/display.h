#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  spi_host;       // SPI2_HOST / SPI3_HOST
    int  gpio_mosi;
    int  gpio_miso;
    int  gpio_sclk;
    int  gpio_cs;
    int  gpio_dc;
    int  gpio_rst;
    int  gpio_bl;
    int  pixel_clock_hz; // e.g. 40 * 1000 * 1000
    int  hres;
    int  vres;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;   // ST7789 INVON; flip if colors look wrong on the panel
} display_config_t;

// Data shown on the main HMI screen.
typedef struct {
    float temperature;
    float setpoint;
    float ssr_duty;       // 0..1
    bool  sensor_fault;
    bool  runaway;
    bool  safety_tripped;
    uint32_t uptime_s;
} display_status_t;

esp_err_t display_init(const display_config_t *cfg);
esp_err_t display_lvgl_lock(int timeout_ms);
void      display_lvgl_unlock(void);

// Build the main HMI screen on the active display.
void display_build_main_screen(void);

// Push fresh status data into the UI (thread-safe wrapper).
void display_update_status(const display_status_t *st);

// Drive the backlight GPIO (no-op if gpio_bl < 0). Called from ui_task
// after the first frame is flushed to avoid showing uninitialised pixels.
void display_set_backlight(bool on);

#ifdef __cplusplus
}
#endif
