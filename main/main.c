// FA-10T v3.0 — Industrial Thermal Controller (ESP32-S3)
//
// Boot sequence:
//   1. NVS init + load config
//   2. SSR driver (idle off)
//   3. MAX31865 SPI bus + device
//   4. Display + LVGL
//   5. Safety subsystem (TWDT + runaway)
//   6. Start FreeRTOS tasks: sensor, control, ui, watchdog

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "driver/spi_master.h"

#include "app_config.h"
#include "app_state.h"
#include "nvs_config.h"
#include "max31865.h"
#include "ssr_driver.h"
#include "display.h"
#include "safety.h"

#include "tasks/sensor_task.h"
#include "tasks/control_task.h"
#include "tasks/ui_task.h"
#include "tasks/watchdog_task.h"

static const char *TAG = "main";

static void log_chip(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    ESP_LOGI(TAG, "ESP32-S3 rev v%d.%d, %d cores, WiFi%s%s, %luMB %s flash",
             info.revision / 100, info.revision % 100,
             info.cores,
             (info.features & CHIP_FEATURE_BT)  ? "/BT"  : "",
             (info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             (unsigned long)spi_flash_get_chip_size() / (1024 * 1024),
             (info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

static esp_err_t init_rtd_bus_and_device(max31865_handle_t *out)
{
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_RTD_MOSI,
        .miso_io_num     = PIN_RTD_MISO,
        .sclk_io_num     = PIN_RTD_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };
    esp_err_t err = spi_bus_initialize(PIN_RTD_SPI_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTD spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    max31865_config_t cfg = {
        .host           = PIN_RTD_SPI_HOST,
        .gpio_cs        = PIN_RTD_CS,
        .gpio_drdy      = PIN_RTD_DRDY,
        .clock_speed_hz = 2 * 1000 * 1000,
        .r_ref_ohms     = RTD_REF_OHMS,
        .r_nominal_ohms = RTD_NOMINAL_OHMS,
        .wiring         = MAX31865_WIRE_3,
        .filter         = MAX31865_FILTER_50HZ,
    };
    return max31865_init(&cfg, out);
}

static esp_err_t init_display(void)
{
    display_config_t cfg = {
        .spi_host       = PIN_TFT_SPI_HOST,
        .gpio_mosi      = PIN_TFT_MOSI,
        .gpio_miso      = PIN_TFT_MISO,
        .gpio_sclk      = PIN_TFT_SCLK,
        .gpio_cs        = PIN_TFT_CS,
        .gpio_dc        = PIN_TFT_DC,
        .gpio_rst       = PIN_TFT_RST,
        .gpio_bl        = PIN_TFT_BL,
        .pixel_clock_hz = TFT_PIXEL_CLOCK_HZ,
        .hres           = TFT_HRES,
        .vres           = TFT_VRES,
        .swap_xy        = false,
        .mirror_x       = false,
        .mirror_y       = false,
    };
    return display_init(&cfg);
}

void app_main(void)
{
    log_chip();
    ESP_LOGI(TAG, "FA-10T v3.0 firmware boot");

    // 1. NVS + config
    ESP_ERROR_CHECK(nvs_config_init());
    app_state_init();

    fa10t_config_t cfg;
    nvs_config_load(&cfg);
    app_state_set_config(&cfg);

    // 2. SSR (start in OFF state, before any heat-call path is alive)
    ssr_driver_config_t ssr_cfg = {
        .gpio        = PIN_SSR,
        .period_ms   = cfg.ssr_period_ms ? cfg.ssr_period_ms : SSR_PERIOD_MS,
        .active_high = SSR_ACTIVE_HIGH,
    };
    ESP_ERROR_CHECK(ssr_driver_init(&ssr_cfg));
    ssr_driver_set_duty(0.0f);

    // 3. RTD
    max31865_handle_t rtd = NULL;
    ESP_ERROR_CHECK(init_rtd_bus_and_device(&rtd));

    // 4. Display + LVGL
    ESP_ERROR_CHECK(init_display());

    // 5. Safety
    safety_config_t sc = {
        .temp_max_c       = cfg.temp_max_c       > 0 ? cfg.temp_max_c       : SAFETY_TEMP_MAX_C,
        .runaway_dt_c     = cfg.runaway_dt_c     > 0 ? cfg.runaway_dt_c     : SAFETY_RUNAWAY_DT_C,
        .runaway_window_s = cfg.runaway_window_s > 0 ? cfg.runaway_window_s : SAFETY_RUNAWAY_WINDOW_S,
        .runaway_duty_thr = SAFETY_RUNAWAY_DUTY_THR,
        .hysteresis_c     = SAFETY_HYSTERESIS_C,
        .recovery_ramp_s  = SAFETY_RECOVERY_RAMP_S,
        .wdt_timeout_s    = SAFETY_WDT_TIMEOUT_S,
    };
    ESP_ERROR_CHECK(safety_init(&sc));

    // 6. Tasks
    sensor_task_start(rtd);
    control_task_start();
    ui_task_start();
    watchdog_task_start();

    ESP_LOGI(TAG, "boot complete; setpoint=%.1f°C", cfg.setpoint);
}
