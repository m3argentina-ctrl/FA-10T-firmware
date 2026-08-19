#include "sensor_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"
#include "ds18b20_bus.h"
#include "acs712.h"
#include "sht31.h"

static const char *TAG = "sensor_task";

static TaskHandle_t s_handle;
TaskHandle_t sensor_task_handle(void) { return s_handle; }

// Sub-rate counters: temperatura cada ciclo, ACS712 cada 5 (2 Hz), SHT31 cada 10 (1 Hz).
#define ACS_DIVIDER  (500  / SENSOR_TASK_PERIOD_MS)   // 5
#define SHT_DIVIDER  (1000 / SENSOR_TASK_PERIOD_MS)   // 10

// --- Filtro de temperatura: mediana de N + EMA -------------------------------
// Solo se alimenta con lecturas VÁLIDAS (una falla no ensucia el filtro; al
// volver el sensor, sigue desde el último valor bueno). Con el DS18B20 (digital)
// la mediana solo sirve para matar algún glitch del bus 1-Wire.
static float s_med_buf[DS18B20_MEDIAN_N];
static int   s_med_count;      // muestras cargadas (arranque)
static int   s_med_idx;
static float s_ema;
static bool  s_ema_ready;

static float temp_filter(float raw, float dt_s)
{
    // Mediana de las últimas N (copia + insertion sort: N=5, trivial).
    s_med_buf[s_med_idx] = raw;
    s_med_idx = (s_med_idx + 1) % DS18B20_MEDIAN_N;
    if (s_med_count < DS18B20_MEDIAN_N) s_med_count++;

    float tmp[DS18B20_MEDIAN_N];
    for (int i = 0; i < s_med_count; ++i) tmp[i] = s_med_buf[i];
    for (int i = 1; i < s_med_count; ++i) {
        float v = tmp[i]; int j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; --j; }
        tmp[j + 1] = v;
    }
    float med = tmp[s_med_count / 2];

    // EMA con tau fijo (dt constante a 10 Hz).
    if (!s_ema_ready) { s_ema = med; s_ema_ready = true; }
    else {
        float alpha = dt_s / (DS18B20_FILTER_TAU_S + dt_s);
        s_ema += (med - s_ema) * alpha;
    }
    return s_ema;
}

static void sensor_task(void *arg)
{
    (void)arg;
    QueueHandle_t q = app_state_sensor_queue();
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0;

    safety_wdt_subscribe();

    {   // Diagnóstico: si quedó una calibración vieja del PT1000 en NVS, escala
        // la lectura del DS18B20 y puede disparar alarmas falsas.
        fa10t_config_t c0;
        app_state_copy_config(&c0);
        ESP_LOGI(TAG, "calibracion NVS: gain=%.4f offset=%.2f",
                 (double)c0.cal_gain, (double)c0.cal_offset);
    }

    while (1) {
        // --- Temperatura: DS18B20 (1-Wire) — v3, reemplaza al PT1000 ---
        // Driver NO bloqueante: dispara la conversión y la lee ~750 ms después;
        // entre medio repite el último valor. La térmica del horno es de minutos,
        // así que el ~1 Hz efectivo sobra para el PID y la seguridad.
        ds18b20_reading_t r = {0};
        esp_err_t err = ds18b20_bus_read(&r);

        // Debounce: un glitch aislado del bus 1-Wire NO debe cortar las salidas.
        // Sólo se declara falla tras N ciclos malos seguidos.
        const bool raw_fault = (err != ESP_OK) || r.fault;
        static int s_fault_run;
        if (raw_fault) {
            if (s_fault_run < SENSOR_FAULT_DEBOUNCE_N) s_fault_run++;
        } else {
            s_fault_run = 0;
        }
        const bool fault_debounced = (s_fault_run >= SENSOR_FAULT_DEBOUNCE_N);

        sensor_sample_t sample = {
            .timestamp_us      = esp_timer_get_time(),
            .fault             = fault_debounced,
            .fault_status      = r.fault_status,
            .raw_temperature   = r.temperature_c,
            .limit_temperature = r.max_temperature_c,  // sin calibrar: es safety
        };

        // El DS18B20 es digital (sin el ruido de ADC que tenía el PT1000), pero
        // mantenemos el EMA como suavizado suave. La calibración (lineal) se
        // aplica sobre el valor filtrado; el asistente de calibración de 2 puntos
        // captura raw_temperature y necesita un valor estable.
        // Sólo se alimenta el filtro con lecturas VÁLIDAS (mirando raw_fault, no
        // el debounce): así un glitch no ensucia el valor, y mientras dura se
        // sigue publicando el último bueno.
        static float s_last_good = 0.0f;
        if (!raw_fault) {
            s_last_good =
                temp_filter(r.temperature_c, (float)SENSOR_TASK_PERIOD_MS / 1000.0f);
        }
        sample.raw_temperature = s_last_good;

        fa10t_config_t cfg;
        app_state_copy_config(&cfg);
        sample.temperature = cfg.cal_gain * sample.raw_temperature + cfg.cal_offset;

        xQueueOverwrite(q, &sample);

        app_state_lock();
        app_state_get()->last_sample = sample;
        app_state_unlock();

        if (sample.fault) {
            ESP_LOGW(TAG, "DS18B20 fault status=0x%02X err=%s T=%.1f n=%d",
                     r.fault_status, esp_err_to_name(err), r.temperature_c, r.sensor_count);
        }

        // --- ACS712 @ 2 Hz ---
        if ((tick % ACS_DIVIDER) == 0) {
            float amps = 0.0f;
            if (acs712_read_rms(&amps) == ESP_OK) {
                app_state_lock();
                app_state_t *st = app_state_get();
                st->fan_current = amps;
                if (st->fan_nominal_known && st->fan_nominal > 0.05f) {
                    st->fan_fault = (amps < ACS712_FAULT_RATIO * st->fan_nominal);
                } else {
                    st->fan_fault = false;
                }
                app_state_unlock();
            }
        }

        // --- SHT31 @ 1 Hz (con gestión del heater anti-condensación) ---
        if ((tick % SHT_DIVIDER) == 0) {
            float rh = 0.0f, t_amb = 0.0f;
            bool  valid = false;
            esp_err_t serr = sht31_read_managed(&rh, &t_amb, &valid);
            app_state_lock();
            app_state_t *st = app_state_get();
            if (serr == ESP_OK && valid) {
                st->humidity       = rh;
                st->sht31_temp     = t_amb;
                st->humidity_fault = false;
            } else if (serr != ESP_OK) {
                st->humidity_fault = true;
            }
            // Si !valid (heater encendido/enfriando): se mantiene el último valor
            // válido, sin marcar fault.
            app_state_unlock();
            if (serr != ESP_OK) {
                ESP_LOGW(TAG, "SHT31 read failed: %s", esp_err_to_name(serr));
            }
        }

        tick++;
        safety_wdt_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensor_task_start(void)
{
    xTaskCreatePinnedToCore(sensor_task, "sensor",
                            SENSOR_TASK_STACK, NULL,
                            SENSOR_TASK_PRIO, &s_handle,
                            SENSOR_TASK_CORE);
}
