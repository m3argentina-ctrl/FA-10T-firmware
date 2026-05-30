#include "acs712.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_config.h"

#if ACS712_ENABLED
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

static const char *TAG = "acs712";

#if ACS712_ENABLED
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static SemaphoreHandle_t         s_mtx;
static bool                      s_cali_ready;
#endif
static bool                      s_ready;

esp_err_t acs712_init(void)
{
    if (s_ready) return ESP_OK;

#if !ACS712_ENABLED
    // TODO v2: reactivar ACS712 vía ADS1115 por I2C cuando el HW lo permita.
    // En la PCB actual GPIO10 colisiona con SD_MOSI del board Waveshare, así
    // que el driver queda inerte: read_rms() publica un valor nominal mock
    // para que la lógica de "falla de turbina" en sensor_task.c (compara
    // amps < ACS712_FAULT_RATIO * nominal) jamás se dispare.
    s_ready = true;
    ESP_LOGW(TAG, "ACS712 DISABLED (HW conflict GPIO10/SD_MOSI) — fan-fault inactive, "
                  "publishing mock %.2f A", ACS712_MOCK_NOMINAL_A);
    return ESP_OK;
#else
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

#if SENSORS_FAKE
    s_ready = true;
    ESP_LOGI(TAG, "ACS712 init [SENSORS_FAKE]");
    return ESP_OK;
#else
    adc_oneshot_unit_init_cfg_t init = {
        .unit_id = ACS712_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init, &s_adc), TAG, "new_unit");

    adc_oneshot_chan_cfg_t chan = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, ACS712_ADC_CHANNEL, &chan),
                        TAG, "config_channel");

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ACS712_ADC_UNIT,
        .chan     = ACS712_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t cerr = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    s_cali_ready = (cerr == ESP_OK);
    if (!s_cali_ready) {
        ESP_LOGW(TAG, "ADC cali curve-fitting unavailable (%s) — falling back to raw scaling",
                 esp_err_to_name(cerr));
    }

    s_ready = true;
    ESP_LOGI(TAG, "ACS712 init: GPIO%d (ADC%d ch%d) sens=%.0fmV/A",
             PIN_ACS712_ADC, (int)ACS712_ADC_UNIT + 1, (int)ACS712_ADC_CHANNEL,
             ACS712_SENS_V_PER_A * 1000.0f);
    return ESP_OK;
#endif // !SENSORS_FAKE
#endif // ACS712_ENABLED
}

#if ACS712_ENABLED && !SENSORS_FAKE
static esp_err_t read_voltage(float *v_out)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, ACS712_ADC_CHANNEL, &raw);
    if (err != ESP_OK) return err;
    int mv = 0;
    if (s_cali_ready) {
        err = adc_cali_raw_to_voltage(s_cali, raw, &mv);
        if (err != ESP_OK) return err;
    } else {
        mv = raw * 3100 / 4095;
    }
    *v_out = (float)mv / 1000.0f;
    return ESP_OK;
}
#endif

esp_err_t acs712_read_rms(float *amps_out)
{
    if (!s_ready || !amps_out) return ESP_ERR_INVALID_STATE;

#if !ACS712_ENABLED
    // TODO v2: reemplazar por lectura real desde ADS1115.
    *amps_out = ACS712_MOCK_NOMINAL_A;
    return ESP_OK;
#else
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    esp_err_t ret;
#if SENSORS_FAKE
    // Pretend fans draw the configured nominal with ~3% white noise.
    extern bool   app_fan_is_on_for_sim(void);
    const bool on = app_fan_is_on_for_sim();
    if (!on) {
        *amps_out = 0.0f;
        ret = ESP_OK;
    } else {
        float noise = ((float)(esp_random() & 0xFF) - 128.0f) * (0.03f / 128.0f);
        *amps_out = 0.45f * (1.0f + noise);
        ret = ESP_OK;
    }
#else
    double sq_sum = 0.0;
    int    n      = 0;
    for (int i = 0; i < ACS712_RMS_SAMPLES; ++i) {
        float v = 0.0f;
        if (read_voltage(&v) != ESP_OK) continue;
        float dv = v - ACS712_VREF_V;
        sq_sum += (double)(dv * dv);
        n++;
        esp_rom_delay_us(ACS712_SAMPLE_INTERVAL_US);
    }
    if (n == 0) {
        ret = ESP_FAIL;
    } else {
        float vrms = sqrtf((float)(sq_sum / (double)n));
        *amps_out  = vrms / ACS712_SENS_V_PER_A;
        ret        = ESP_OK;
    }
#endif
    xSemaphoreGive(s_mtx);
    return ret;
#endif // ACS712_ENABLED
}

esp_err_t acs712_learn_nominal(float *nominal_out)
{
    if (!s_ready || !nominal_out) return ESP_ERR_INVALID_STATE;

#if !ACS712_ENABLED
    // TODO v2: aprender nominal real con ADS1115 + transformador de corriente.
    // Devolvemos el mock con ESP_OK para que main.c marque fan_nominal_known=true
    // y la UI vea valores coherentes; sensor_task siempre verá amps ≈ nominal,
    // así que fan_fault permanece en false.
    *nominal_out = ACS712_MOCK_NOMINAL_A;
    ESP_LOGW(TAG, "learn_nominal skipped (ACS712 disabled) — mock %.3f A", *nominal_out);
    return ESP_OK;
#else
    const TickType_t start = xTaskGetTickCount();
    const TickType_t window = pdMS_TO_TICKS(ACS712_LEARN_DURATION_MS);
    float sum = 0.0f;
    int n = 0;
    while ((xTaskGetTickCount() - start) < window) {
        float a = 0.0f;
        if (acs712_read_rms(&a) == ESP_OK) {
            sum += a;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (n == 0) return ESP_FAIL;
    *nominal_out = sum / (float)n;
    ESP_LOGI(TAG, "learned nominal = %.3f A (%d samples)", *nominal_out, n);
    return ESP_OK;
#endif
}
