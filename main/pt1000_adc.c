#include "pt1000_adc.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "app_config.h"

static const char *TAG = "pt1000";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static adc_unit_t                s_unit;
static adc_channel_t             s_chan;
static bool                      s_ready;
static bool                      s_cali_ready;

#if SENSORS_FAKE
// Synthesise a first-order thermal ramp toward the live setpoint so the
// control loop has something to chase. tau ~ 120 s.
static float    s_sim_temp = 25.0f;
static uint64_t s_sim_last_us;
#endif

esp_err_t pt1000_adc_init(void)
{
    if (s_ready) return ESP_OK;

#if SENSORS_FAKE
    s_sim_temp    = 25.0f;
    s_sim_last_us = esp_timer_get_time();
    s_ready       = true;
    ESP_LOGI(TAG, "PT1000 init [SENSORS_FAKE]");
    return ESP_OK;
#else
    // Resolve ADC unit + channel from the GPIO so changing PIN_PT1000_ADC in
    // app_config.h is enough — no need to also update macros for unit/channel.
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(PIN_PT1000_ADC, &s_unit, &s_chan),
                        TAG, "io_to_channel(GPIO%d)", PIN_PT1000_ADC);

    adc_oneshot_unit_init_cfg_t init = {
        .unit_id = s_unit,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init, &s_adc), TAG, "new_unit");

    adc_oneshot_chan_cfg_t chan = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_chan, &chan),
                        TAG, "config_channel");

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = s_unit,
        .chan     = s_chan,
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
    ESP_LOGI(TAG, "PT1000 init: GPIO%d (ADC%d ch%d), Rtop=%.0fΩ Rser=%.0fΩ",
             PIN_PT1000_ADC, (int)s_unit + 1, (int)s_chan,
             PT1000_R_TOP_OHMS, PT1000_R_SERIES_OHMS);
    return ESP_OK;
#endif
}

// Temperature from resistance via Callendar-Van Dusen (T ≥ 0°C branch).
static float r_to_temp_c(float r)
{
    const float A = PT1000_CVD_A;
    const float B = PT1000_CVD_B;
    const float R0 = PT1000_R0_OHMS;
    float disc = A * A - 4.0f * B * (1.0f - r / R0);
    if (disc < 0.0f) disc = 0.0f;
    return (-A + sqrtf(disc)) / (2.0f * B);
}

#if !SENSORS_FAKE
static float v_to_resistance(float v)
{
    // V_adc = (Rser + Rpt) * Vref / (Rtop + Rser + Rpt)
    // Rpt = V_adc * Rtop / (Vref - V_adc) - Rser
    float denom = PT1000_VREF_V - v;
    if (denom < 0.001f) denom = 0.001f;   // clamp; flagged as OPEN below
    return v * PT1000_R_TOP_OHMS / denom - PT1000_R_SERIES_OHMS;
}
#endif

esp_err_t pt1000_adc_read(pt1000_reading_t *out)
{
    if (!s_ready || !out) return ESP_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));

#if SENSORS_FAKE
    extern float app_setpoint_for_sim(void);
    extern float app_drv_duty_for_sim(void);
    const float sp     = app_setpoint_for_sim();
    const float duty   = app_drv_duty_for_sim();
    const uint64_t now = esp_timer_get_time();
    float dt = (now - s_sim_last_us) / 1.0e6f;
    if (dt < 0.0f || dt > 1.0f) dt = 0.1f;
    s_sim_last_us = now;
    // Modelo térmico de primer orden que respeta el SSR_DRV:
    //   - heater ON  (duty > 5%): tiende al setpoint, tau_heat = 15 s.
    //   - heater OFF (duty ≤ 5%): se enfría hacia 25 °C, tau_cool = 600 s.
    //
    // tau_heat bajo (15 s) hace que el warm-up termine en ~45 s y el ciclo
    // simulado se pueda probar sin esperar minutos. La planta real tiene
    // tau ≈ 10–15 min.
    // tau_cool muy lento (10 min) simula la masa térmica de un horno: cuando
    // el PID modula cerca del SP y el output cae momentáneamente a 0, la T
    // casi no baja → se mantiene cerca del SP. Sin eso, el warm-up no
    // terminaba nunca por la dinámica del PID con Kd alto.
    const float tau_heat = 15.0f;
    const float tau_cool = 600.0f;
    if (duty > 0.05f) {
        s_sim_temp += (sp - s_sim_temp) * (dt / tau_heat);
    } else {
        s_sim_temp += (25.0f - s_sim_temp) * (dt / tau_cool);
    }
    // Tiny noise so derivative path is exercised
    float noise = ((float)(esp_random() & 0xFF) - 128.0f) * (0.01f / 128.0f);
    out->temperature_c   = s_sim_temp + noise;
    out->resistance_ohms = PT1000_R0_OHMS * (1.0f + PT1000_CVD_A * out->temperature_c);
    out->voltage_v       = (PT1000_R_SERIES_OHMS + out->resistance_ohms) * PT1000_VREF_V /
                           (PT1000_R_TOP_OHMS + PT1000_R_SERIES_OHMS + out->resistance_ohms);
    out->fault         = false;
    out->fault_status  = 0;
    return ESP_OK;
#else
    // Oversample
    int32_t acc_mv = 0;
    int valid = 0;
    for (int i = 0; i < PT1000_OVERSAMPLE_N; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc, s_chan, &raw);
        if (err != ESP_OK) continue;
        int mv = 0;
        if (s_cali_ready) {
            if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
        } else {
            // Fallback: assume linear 0..3100 mV across 12-bit range
            mv = raw * 3100 / 4095;
        }
        acc_mv += mv;
        valid++;
    }
    if (valid == 0) {
        out->fault        = true;
        out->fault_status = PT1000_FAULT_ADC_ERR;
        return ESP_OK;
    }
    float v = (float)acc_mv / (float)valid / 1000.0f;
    out->voltage_v = v;

    if (v >= PT1000_FAULT_VOPEN_V) {
        out->fault        = true;
        out->fault_status = PT1000_FAULT_OPEN;
        return ESP_OK;
    }
    if (v <= PT1000_FAULT_VSHORT_V) {
        out->fault        = true;
        out->fault_status = PT1000_FAULT_SHORT;
        return ESP_OK;
    }

    float r = v_to_resistance(v);
    out->resistance_ohms = r;
    float t = r_to_temp_c(r);
    out->temperature_c = t;

    if (t < PT1000_FAULT_TMIN_C || t > PT1000_FAULT_TMAX_C) {
        out->fault        = true;
        out->fault_status = PT1000_FAULT_OUT_RANGE;
    }
    return ESP_OK;
#endif
}
