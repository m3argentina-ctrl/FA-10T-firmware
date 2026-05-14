#include "max31865.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "max31865";

struct max31865_dev_s {
    spi_device_handle_t spi;
    max31865_config_t   cfg;
    uint8_t             config_reg;
};

static esp_err_t reg_write(max31865_handle_t h, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), val };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_transmit(h->spi, &t);
}

static esp_err_t reg_read(max31865_handle_t h, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t addr = reg & 0x7F;
    spi_transaction_t t_addr = {
        .length    = 8,
        .tx_buffer = &addr,
    };
    esp_err_t err = spi_device_polling_transmit(h->spi, &t_addr);
    if (err != ESP_OK) return err;

    spi_transaction_t t_data = {
        .length    = len * 8,
        .rxlength  = len * 8,
        .rx_buffer = buf,
    };
    return spi_device_polling_transmit(h->spi, &t_data);
}

static esp_err_t apply_config(max31865_handle_t h)
{
    uint8_t cfg = MAX31865_CFG_VBIAS | MAX31865_CFG_CONV_AUTO;
    if (h->cfg.wiring == MAX31865_WIRE_3)  cfg |= MAX31865_CFG_3WIRE;
    if (h->cfg.filter == MAX31865_FILTER_50HZ) cfg |= MAX31865_CFG_FILTER_50HZ;
    h->config_reg = cfg;
    return reg_write(h, MAX31865_REG_CONFIG, cfg);
}

esp_err_t max31865_init(const max31865_config_t *cfg, max31865_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "null args");
    ESP_RETURN_ON_FALSE(cfg->r_ref_ohms > 0.0f && cfg->r_nominal_ohms > 0.0f,
                        ESP_ERR_INVALID_ARG, TAG, "bad resistances");

    max31865_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    memcpy(&h->cfg, cfg, sizeof(*cfg));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clock_speed_hz > 0 ? cfg->clock_speed_hz : 2 * 1000 * 1000,
        .mode           = 1, // CPOL=0, CPHA=1
        .spics_io_num   = cfg->gpio_cs,
        .queue_size     = 2,
        .cs_ena_pretrans  = 1,
        .cs_ena_posttrans = 1,
    };

    esp_err_t err = spi_bus_add_device(cfg->host, &dev, &h->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    if (cfg->gpio_drdy >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->gpio_drdy,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&io);
    }

    err = apply_config(h);
    if (err != ESP_OK) {
        spi_bus_remove_device(h->spi);
        free(h);
        return err;
    }

    // First conversion needs settle time after VBIAS on
    vTaskDelay(pdMS_TO_TICKS(70));

    *out_handle = h;
    ESP_LOGI(TAG, "MAX31865 initialised (cs=%d, drdy=%d, Rref=%.1fΩ, R0=%.1fΩ)",
             cfg->gpio_cs, cfg->gpio_drdy, cfg->r_ref_ohms, cfg->r_nominal_ohms);
    return ESP_OK;
}

esp_err_t max31865_deinit(max31865_handle_t h)
{
    if (!h) return ESP_OK;
    if (h->spi) spi_bus_remove_device(h->spi);
    free(h);
    return ESP_OK;
}

esp_err_t max31865_set_bias(max31865_handle_t h, bool enable)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    if (enable) h->config_reg |=  MAX31865_CFG_VBIAS;
    else        h->config_reg &= ~MAX31865_CFG_VBIAS;
    return reg_write(h, MAX31865_REG_CONFIG, h->config_reg);
}

esp_err_t max31865_clear_fault(max31865_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    uint8_t v = (h->config_reg & ~MAX31865_CFG_1SHOT) | MAX31865_CFG_FAULT_CLEAR;
    return reg_write(h, MAX31865_REG_CONFIG, v);
}

esp_err_t max31865_read(max31865_handle_t h, max31865_reading_t *out)
{
    if (!h || !out) return ESP_ERR_INVALID_ARG;

    uint8_t rtd[2] = {0};
    esp_err_t err = reg_read(h, MAX31865_REG_RTD_MSB, rtd, 2);
    if (err != ESP_OK) return err;

    uint16_t raw = ((uint16_t)rtd[0] << 8) | rtd[1];
    bool fault   = (raw & 0x0001) != 0;
    uint16_t adc = raw >> 1; // 15 bits

    out->raw_rtd     = adc;
    out->fault       = fault;
    out->fault_status = 0;

    // Resistance = (ADC * Rref) / 2^15
    out->resistance = ((float)adc * h->cfg.r_ref_ohms) / 32768.0f;
    out->temperature = max31865_resistance_to_temperature(out->resistance, h->cfg.r_nominal_ohms);

    if (fault) {
        uint8_t fs = 0;
        if (reg_read(h, MAX31865_REG_FAULT_STATUS, &fs, 1) == ESP_OK) {
            out->fault_status = fs;
        }
        ESP_LOGW(TAG, "fault detected: status=0x%02X raw=0x%04X", out->fault_status, raw);
    }
    return ESP_OK;
}

float max31865_resistance_to_temperature(float r, float r0)
{
    // Callendar–Van Dusen coefficients (IEC 60751)
    const float A = 3.9083e-3f;
    const float B = -5.775e-7f;
    // For T >= 0 °C:  R = R0 (1 + A·T + B·T²)
    // T = (-A + sqrt(A² - 4B(1 - R/R0))) / (2B)
    float Z1 = -A;
    float Z2 = A * A - 4.0f * B;
    float Z3 = (4.0f * B) / r0;
    float Z4 = 2.0f * B;

    float t_pos = (Z1 + sqrtf(Z2 + Z3 * r)) / Z4;
    if (t_pos >= 0.0f) return t_pos;

    // For T < 0 °C use polynomial approximation
    float rpoly = r;
    float t = -242.02f;
    t +=  2.2228f * rpoly;
    rpoly *= r;  t += 2.5859e-3f  * rpoly;
    rpoly *= r;  t -= 4.8260e-6f  * rpoly;
    rpoly *= r;  t -= 2.8183e-8f  * rpoly;
    rpoly *= r;  t += 1.5243e-10f * rpoly;
    return t;
}
