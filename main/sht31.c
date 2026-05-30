#include "sht31.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"

static const char *TAG = "sht31";

static i2c_master_bus_handle_t  s_bus;
static i2c_master_dev_handle_t  s_dev;
static bool                     s_ready;

#if SENSORS_FAKE
static uint64_t s_sim_t0_us;
#endif

esp_err_t sht31_init(void)
{
    if (s_ready) return ESP_OK;

#if SIMULATION_MODE
    // Modo totalmente simulado: ni siquiera intentamos crear el bus I2C.
    s_sim_t0_us = esp_timer_get_time();
    s_ready     = true;
    ESP_LOGI(TAG, "SHT31 init [SIMULATION_MODE — sin bus I2C]");
    return ESP_OK;
#else
    // El bus I2C SIEMPRE se crea: lo usan también display (TCA9554 + FT6336)
    // y audio (ES8311). El flag SENSORS_SIMULATION solo gobierna el device
    // SHT31 y las lecturas.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port       = I2C_BUS_PORT,
        .sda_io_num     = PIN_I2C_SDA,
        .scl_io_num     = PIN_I2C_SCL,
        .clk_source     = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "new_bus");

  #if SENSORS_SIMULATION
    s_sim_t0_us = esp_timer_get_time();
    s_ready     = true;
    ESP_LOGI(TAG, "SHT31 init [SENSORS_SIMULATION] — bus I2C arriba, device mock");
    return ESP_OK;
  #else
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SHT31_I2C_ADDR,
        .scl_speed_hz    = I2C_BUS_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "add_dev");

    s_ready = true;
    ESP_LOGI(TAG, "SHT31 init: SDA=%d SCL=%d addr=0x%02X",
             PIN_I2C_SDA, PIN_I2C_SCL, SHT31_I2C_ADDR);
    return ESP_OK;
  #endif
#endif
}

i2c_master_bus_handle_t sht31_shared_bus(void) { return s_bus; }

#if !SENSORS_FAKE
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}
#endif

esp_err_t sht31_read(float *humidity_pct, float *temperature_c)
{
    if (!s_ready || !humidity_pct || !temperature_c) return ESP_ERR_INVALID_STATE;

#if SENSORS_FAKE
    // Drying curve: 70% → 25% over 60 min, asymptotic.
    const float t_min = (float)((esp_timer_get_time() - s_sim_t0_us) / 1000000ULL) / 60.0f;
    float rh = 25.0f + (70.0f - 25.0f) * expf(-t_min / 12.0f);
    float n  = ((float)(esp_random() & 0xFF) - 128.0f) * (0.5f / 128.0f);
    *humidity_pct  = rh + n;
    *temperature_c = 30.0f + n * 0.5f;
    return ESP_OK;
#else
    // Single-shot, high repeatability, no clock stretching: 0x24 0x00
    const uint8_t cmd[2] = { 0x24, 0x00 };
    esp_err_t err = i2c_master_transmit(s_dev, cmd, sizeof(cmd), 100);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t buf[6] = {0};
    err = i2c_master_receive(s_dev, buf, sizeof(buf), 100);
    if (err != ESP_OK) return err;

    if (crc8(&buf[0], 2) != buf[2] || crc8(&buf[3], 2) != buf[5]) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t  = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_rh = ((uint16_t)buf[3] << 8) | buf[4];
    *temperature_c  = -45.0f + 175.0f * (float)raw_t  / 65535.0f;
    *humidity_pct   =          100.0f * (float)raw_rh / 65535.0f;
    return ESP_OK;
#endif
}
