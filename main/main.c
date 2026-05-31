// Control ESP32-S3 v3.0 — Industrial Thermal Controller (ESP32-S3, Waveshare 3.5" LCD)
//
// Boot sequence:
//   1. NVS init + load config
//   2. SSR 3-channel driver (all OFF)
//   3. PT1000 ADC + calibration
//   4. ACS712 current sensor
//   5. SHT31 humidity (also brings up the shared I2C bus)
//   6. Safety subsystem (TWDT + runaway + fan fault)
//   7. Pulse fans on, wait 3 s, learn nominal current, mark ready
//   8. Start FreeRTOS tasks (sensor, control, ui, watchdog)

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs.h"

#include "app_config.h"
#include "app_state.h"
#include "nvs_config.h"
#include "pt1000_adc.h"
#include "acs712.h"
#include "sht31.h"
#include "ssr3ch.h"
#include "safety.h"
#include "telemetry.h"
#include "recovery.h"
#include "programa.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "cloud_telemetry.h"

#include "tasks/sensor_task.h"
#include "tasks/control_task.h"
#include "tasks/ui_task.h"
#include "tasks/watchdog_task.h"

#include "gpio_test.h"

static const char *TAG = "main";

static void log_chip(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "ESP32-S3 rev v%d.%d, %d cores, WiFi%s%s, %luMB %s flash",
             info.revision / 100, info.revision % 100,
             info.cores,
             (info.features & CHIP_FEATURE_BT)  ? "/BT"  : "",
             (info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             (unsigned long)flash_size / (1024 * 1024),
             (info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

// --- Helpers referenced by simulation-mode drivers --------------------------
// pt1000_adc.c & acs712.c declare these as extern. Kept here so the sim path
// does not need to include app_state.h directly.
float app_setpoint_for_sim(void)
{
    // La planta simulada tiende hacia el setpoint efectivo SOLO mientras hay
    // una sesión activa (fan_command_on=true). En reposo tiende a 25 °C de
    // ambiente — si no, la T subiría hacia el setpoint guardado y dispararía
    // SAFETY_RUNAWAY (5 °C / 60 s sin duty SSR).
    app_state_lock();
    const app_state_t *s = app_state_get();
    float sp = s->fan_command_on ? s->effective_setpoint : 25.0f;
    app_state_unlock();
    return sp;
}

bool app_fan_is_on_for_sim(void)
{
    app_state_lock();
    bool on = app_state_get()->ssr_fan_duty > 0.1f;
    app_state_unlock();
    return on;
}

// Duty actual del SSR_DRV (0..1). El sim del PT1000 lo usa para decidir si
// la T debe subir (heater ON) o bajar hacia ambiente (heater OFF). Sin esto,
// la T sube siempre y se dispara SAFETY_RUNAWAY cuando el PID corta duty.
float app_drv_duty_for_sim(void)
{
    app_state_lock();
    float d = app_state_get()->ssr_drv_duty;
    app_state_unlock();
    return d;
}

void app_main(void)
{
    log_chip();

#if GPIO_TEST_MODE
    // Modo de calibración de pinout: ciclar GPIOs del header J8 e imprimir
    // qué pin está activo. NO se inicializa el resto del firmware. Volver a
    // GPIO_TEST_MODE=0 en app_config.h cuando el pinout esté confirmado.
    ESP_LOGW(TAG, "Control ESP32-S3 BOOT EN MODO TEST DE PINOUT — firmware normal omitido");
    gpio_test_loop();   // nunca retorna
    return;
#endif

    ESP_LOGI(TAG, "Control ESP32-S3 v3.0 firmware boot%s%s",
             SIMULATION_MODE   ? " [SIMULATION]"      : "",
             SENSORS_SIMULATION ? " [SENSORS-SIM]"     : "");

    // 1. NVS + config + telemetry / recovery / programs (all NVS-backed)
    ESP_ERROR_CHECK(nvs_config_init());
    app_state_init();

    fa10t_config_t cfg;
    esp_err_t cfg_err = nvs_config_load(&cfg);
    if (cfg_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "running with factory defaults (no saved config)");
    }
    app_state_set_config(&cfg);

    telemetry_init();
    recovery_init();
    programa_init();

    // 2. SSR 3-channel (force-clamp to OFF before any other init can race)
    ESP_ERROR_CHECK(ssr3ch_init());
    ssr3ch_set_duty(SSR_CH_DRV, 0.0f);
    ssr3ch_set_duty(SSR_CH_FAN, 0.0f);
    ssr3ch_set_duty(SSR_CH_AUX, 0.0f);

    // 3. PT1000 ADC
    ESP_ERROR_CHECK(pt1000_adc_init());

    // 4. ACS712 current sensor
    ESP_ERROR_CHECK(acs712_init());

    // 5. SHT31 humidity (also brings up the shared I2C master bus)
    esp_err_t sht_err = sht31_init();
    if (sht_err != ESP_OK) {
        ESP_LOGW(TAG, "SHT31 init failed (%s) — continuing without humidity",
                 esp_err_to_name(sht_err));
    }

    // 6. Safety subsystem. Hardware envelope wins over saved config values.
    safety_config_t sc = {
        .temp_max_c       = SAFETY_TEMP_MAX_C,
        .runaway_dt_c     = SAFETY_RUNAWAY_DT_C,
        .runaway_window_s = SAFETY_RUNAWAY_WINDOW_S,
        .runaway_duty_thr = SAFETY_RUNAWAY_DUTY_THR,
        .hysteresis_c     = SAFETY_HYSTERESIS_C,
        .recovery_ramp_s  = SAFETY_RECOVERY_RAMP_S,
        .wdt_timeout_s    = SAFETY_WDT_TIMEOUT_S,
    };
    ESP_ERROR_CHECK(safety_init(&sc));

    // 7. Energise turbines, give them 3 s to spool up, then learn nominal RMS.
    //    CRÍTICO: esto se hace ANTES de arrancar control_task. Si control ya
    //    estuviera corriendo pondría SSR_CH_FAN=0 cada ciclo (fan_command_on
    //    es false al boot, no hay sesión activa) justo mientras
    //    acs712_learn_nominal() mide → aprendería ~0 A y la detección de falla
    //    de turbina quedaría inútil. Acá nadie compite por el SSR del fan.
    ESP_LOGI(TAG, "spooling turbines to learn nominal current...");
    ssr3ch_set_duty(SSR_CH_FAN, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(500));   // small delay before kicking off learn
    float nominal = 0.0f;
    esp_err_t lerr = acs712_learn_nominal(&nominal);
    if (lerr == ESP_OK && nominal > 0.05f) {
        app_state_lock();
        app_state_get()->fan_nominal       = nominal;
        app_state_get()->fan_nominal_known = true;
        app_state_unlock();
        ESP_LOGI(TAG, "fan nominal current = %.3f A", nominal);
    } else {
        ESP_LOGW(TAG, "fan nominal learn FAILED (%s, A=%.3f) — fan-fault detection disabled",
                 esp_err_to_name(lerr), nominal);
    }
    // Apagar el fan: de acá en más lo gobierna control_task según la sesión.
    ssr3ch_set_duty(SSR_CH_FAN, 0.0f);

    // 8. Tasks
    sensor_task_start();
    control_task_start();
    ui_task_start();
    watchdog_task_start();

    // 9. WiFi (STA). NO es crítico: se inicializa al final para no demorar el
    //    bring-up de seguridad (SSR off + sensores + safety + tasks). Si falla,
    //    sólo se loguea — el controlador sigue operando sin conectividad.
    //    No-op si WIFI_ENABLED=0. Autoconecta si hay credenciales en NVS.
    esp_err_t werr = wifi_manager_init();
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "wifi_manager_init falló (%s) — sin conectividad",
                 esp_err_to_name(werr));
    }

    // 10. Servidor web de monitoreo (solo lectura). No crítico: el httpd escucha
    //     en el stack LwIP y queda alcanzable en cuanto la STA tome IP. No-op si
    //     WEB_SERVER_ENABLED=0. Sobrevive reconexiones WiFi sin reiniciarse.
    esp_err_t serr = web_server_start();
    if (serr != ESP_OK) {
        ESP_LOGW(TAG, "web_server_start falló (%s) — sin dashboard web",
                 esp_err_to_name(serr));
    }

    // 11. Telemetría a la nube (push HTTPS al backend Bio Origen). No crítico:
    //     la tarea espera a que la STA tenga IP antes de empujar. No-op si
    //     CLOUD_TELEMETRY_ENABLED=0 o si no hay identidad de fábrica provista.
    esp_err_t cerr = cloud_telemetry_start();
    if (cerr != ESP_OK) {
        ESP_LOGW(TAG, "cloud_telemetry_start falló (%s) — sin telemetría remota",
                 esp_err_to_name(cerr));
    }

    ESP_LOGI(TAG, "boot complete; setpoint=%.1f°C envelope=%.0f..%.0f°C",
             cfg.setpoint, OPERATING_TEMP_MIN_C, OPERATING_TEMP_MAX_C);
}
