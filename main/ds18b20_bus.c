#include "ds18b20_bus.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "ds18b20";

static onewire_bus_handle_t     s_bus;
static ds18b20_device_handle_t  s_devices[DS18B20_MAX_SENSORS];
static int                      s_dev_count;
static bool                     s_inited;
static volatile bool            s_paused;   // ver ds18b20_bus_set_paused()

// --- Lectura en TAREA PROPIA -------------------------------------------------
// OJO: ds18b20_trigger_temperature_conversion() del componente NO es
// no-bloqueante: hace vTaskDelay() de hasta 800 ms (12 bits) adentro. Con 2
// sondas eso son ~1,6 s de bloqueo. Si se llamara desde sensor_task, éste
// dejaría de publicar muestras, el control_task se quedaría sin datos (timeout
// de 250 ms) y dispararía SENSOR_FAULT cada ciclo.
// Solución: la conversión+lectura corre en esta tarea de baja prioridad, y
// ds18b20_bus_read() sólo devuelve la última copia (retorna al instante).
// La térmica del horno es de minutos, así que ~1 Hz de refresco sobra.
static ds18b20_reading_t  s_last;      // protegido por s_mtx
static SemaphoreHandle_t  s_mtx;

#define DS18B20_TASK_STACK   3584
#define DS18B20_TASK_PRIO    3         // baja: no compite con control/safety
#define DS18B20_TASK_CORE    1

static void ds18b20_task(void *arg);
static void read_all(ds18b20_reading_t *out);

#if SENSORS_FAKE
// Planta térmica simulada de primer orden (portada del viejo pt1000_adc.c):
//   - heater ON  (duty > 5%): tiende al setpoint, tau_heat = 15 s
//   - heater OFF (duty ≤ 5%): se enfría hacia 25 °C, tau_cool = 600 s
// tau_heat bajo hace que el warm-up termine en ~45 s y se pueda probar el ciclo
// sin esperar minutos; la planta real tiene tau ≈ 10-15 min. tau_cool lento
// simula la masa térmica del horno (si no, al cortar el duty el PID vería una
// caída brusca y saltaría RUNAWAY).
static float    s_sim_temp = 25.0f;
static uint64_t s_sim_last_us;

static void sim_step(ds18b20_reading_t *out)
{
    extern float app_setpoint_for_sim(void);
    extern float app_drv_duty_for_sim(void);
    const float sp   = app_setpoint_for_sim();
    const float duty = app_drv_duty_for_sim();
    const uint64_t now = esp_timer_get_time();
    float dt = (now - s_sim_last_us) / 1.0e6f;
    if (dt < 0.0f || dt > 5.0f) dt = 1.0f;
    s_sim_last_us = now;

    const float target = (duty > 0.05f) ? sp   : 25.0f;
    const float tau    = (duty > 0.05f) ? 15.0f : 600.0f;
    s_sim_temp += (target - s_sim_temp) * (dt / (tau + dt));

    memset(out, 0, sizeof(*out));
    out->sensor_count     = 2;
    out->temperature_c    = s_sim_temp;
    out->max_temperature_c = s_sim_temp;
}
#endif

static ds18b20_resolution_t resolution_enum(void)
{
#if DS18B20_RESOLUTION_BITS >= 12
    return DS18B20_RESOLUTION_12B;
#elif DS18B20_RESOLUTION_BITS == 11
    return DS18B20_RESOLUTION_11B;
#elif DS18B20_RESOLUTION_BITS == 10
    return DS18B20_RESOLUTION_10B;
#else
    return DS18B20_RESOLUTION_9B;
#endif
}

esp_err_t ds18b20_bus_init(void)
{
#if SENSORS_FAKE
    s_sim_temp    = 25.0f;
    s_sim_last_us = esp_timer_get_time();
    s_dev_count   = 2;              // simula las 2 sondas del equipo
    ESP_LOGI(TAG, "DS18B20 init [SENSORS_FAKE] — sin bus 1-Wire");
#else
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = PIN_ONEWIRE,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,   // 1 byte ROM cmd + 8 bytes ROM + 1 byte device cmd
    };

    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "onewire bus init (GPIO%d): %s", PIN_ONEWIRE, esp_err_to_name(err));
        return err;
    }

    // Escaneo del bus (multidrop).
    s_dev_count = 0;
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t dev;
    if (onewire_new_device_iter(s_bus, &iter) == ESP_OK) {
        while (s_dev_count < DS18B20_MAX_SENSORS &&
               onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
            ds18b20_config_t ds_cfg = { };   // struct de config vacío en el componente
            ds18b20_device_handle_t h = NULL;
            if (ds18b20_new_device(&dev, &ds_cfg, &h) == ESP_OK) {
                ds18b20_set_resolution(h, resolution_enum());
                s_devices[s_dev_count] = h;
                ESP_LOGI(TAG, "DS18B20[%d] ROM=0x%016llX", s_dev_count,
                         (unsigned long long)dev.address);
                s_dev_count++;
            } else {
                ESP_LOGW(TAG, "dispositivo 1-Wire no-DS18B20 ignorado (ROM=0x%016llX)",
                         (unsigned long long)dev.address);
            }
        }
        onewire_del_device_iter(iter);
    }

    ESP_LOGI(TAG, "DS18B20 detectados: %d", s_dev_count);
#endif  // SENSORS_FAKE

    memset(&s_last, 0, sizeof(s_last));
    s_last.sensor_count = s_dev_count;
    // Arranca en fault hasta que la tarea publique la primera lectura válida:
    // así el equipo no cree que está a 0 °C durante el primer segundo.
    s_last.fault        = true;
    s_last.fault_status = (s_dev_count == 0) ? DS18B20_FAULT_NO_SENSOR
                                             : DS18B20_FAULT_BUS_ERR;

    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        if (!s_mtx) {
            ESP_LOGE(TAG, "no se pudo crear el mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    s_inited = true;

    if (s_dev_count > 0) {
        // Primera lectura SÍNCRONA antes de arrancar la tarea: si no, durante
        // ~1,6 s el driver reporta falla, la safety dispara SENSOR_FAULT y el
        // equipo queda en RUN_STATE_ALARM apenas bootea (falsa alarma).
        // Cuesta ~1,6 s de boot y evita ese arranque en alarma.
#if !SENSORS_FAKE
        read_all(&s_last);
        ESP_LOGI(TAG, "primera lectura: T=%.2f max=%.2f fault=%d",
                 (double)s_last.temperature_c, (double)s_last.max_temperature_c,
                 (int)s_last.fault);
#endif
        xTaskCreatePinnedToCore(ds18b20_task, "ds18b20",
                                DS18B20_TASK_STACK, NULL,
                                DS18B20_TASK_PRIO, NULL, DS18B20_TASK_CORE);
    }
    return ESP_OK;
}

// Lee TODAS las sondas (modelo 2 sondas arriba/abajo):
//   temperature_c     = PROMEDIO de las sondas VÁLIDAS  -> alimenta el PID.
//   max_temperature_c = la MÁS CALIENTE                 -> la usa la seguridad.
// Si una sonda falla se sigue con las buenas; solo si fallan TODAS -> fault
// (fail-safe: no parar el equipo por una sola sonda muerta).
static void read_all(ds18b20_reading_t *out)
{
    memset(out, 0, sizeof(*out));
    out->sensor_count = s_dev_count;

    if (s_dev_count == 0) {
        out->fault        = true;
        out->fault_status = DS18B20_FAULT_NO_SENSOR;
        return;
    }

    float sum   = 0.0f;
    float max_t = 0.0f;
    int   n_valid = 0;

    for (int i = 0; i < s_dev_count; ++i) {
        float t = 0.0f;
        // Patrón del componente (ver ejemplo oficial): trigger — que ya espera
        // internamente el tiempo de conversión — e inmediatamente después leer.
        esp_err_t err = ds18b20_trigger_temperature_conversion(s_devices[i]);
        if (err == ESP_OK) {
            err = ds18b20_get_temperature(s_devices[i], &t);
        }
        bool valid = (err == ESP_OK) &&
                     (t >= DS18B20_FAULT_TMIN_C) && (t <= DS18B20_FAULT_TMAX_C);
        if (valid) {
            if (n_valid == 0 || t > max_t) max_t = t;
            sum += t;
            n_valid++;
        }
    }

    if (n_valid == 0) {
        out->fault        = true;
        out->fault_status = DS18B20_FAULT_BUS_ERR;
        return;
    }

    out->temperature_c     = sum / (float)n_valid;   // promedio -> PID
    out->max_temperature_c = max_t;                  // más caliente -> seguridad
}

// Tarea de fondo: hace las conversiones (que bloquean ~800 ms por sonda) sin
// frenar al sensor_task. Publica el resultado en s_last bajo mutex.
static void ds18b20_task(void *arg)
{
    (void)arg;
    ds18b20_reading_t r;
    while (1) {
        // En pausa (p. ej. durante una actualización OTA) no se toca el bus:
        // se conserva la última lectura buena y no se generan fallas falsas.
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
#if SENSORS_FAKE
        sim_step(&r);
        vTaskDelay(pdMS_TO_TICKS(200));
#else
        read_all(&r);                       // bloqueante: ~0,8 s por sonda
        vTaskDelay(pdMS_TO_TICKS(50));      // respiro entre ciclos
#endif
        if (s_paused) continue;             // pausó durante la conversión
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_last = r;
        xSemaphoreGive(s_mtx);
    }
}

void ds18b20_bus_set_paused(bool paused)
{
    if (s_paused == paused) return;
    s_paused = paused;
    ESP_LOGW(TAG, "bus 1-Wire %s", paused ? "EN PAUSA (OTA en curso)" : "reanudado");
}

esp_err_t ds18b20_bus_read(ds18b20_reading_t *out)
{
    if (!s_inited || !out) return ESP_ERR_INVALID_STATE;
    // Sólo copia el último valor publicado por la tarea: retorna al instante.
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_last;
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}
