#pragma once

#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_types.h"

// --- Build mode -------------------------------------------------------------
// SIMULATION_MODE=1: TODO simulado (incluye display/touch/audio fake). Útil
// para correr el firmware en host o sin hardware conectado.
#ifndef SIMULATION_MODE
#define SIMULATION_MODE              0
#endif

// SENSORS_SIMULATION=1: drivers de sensores (PT1000, SHT31, ACS712) devuelven
// valores sintéticos plausibles, pero el display + touch + audio + bus I2C
// siguen funcionando contra hardware real. Sirve para probar la UI en el
// equipo cuando todavía no está conectada la PCB FA-10T con los sensores
// físicos. La planta simulada calienta hacia el setpoint con tau ≈ 120 s, la
// humedad baja de 70%→25% en 12 min, la corriente de turbina queda fija en
// nominal. Los SSR SÍ se siguen controlando (output real al GPIO) — apagar
// si la PCB de potencia está conectada y no querés energizar nada.
#ifndef SENSORS_SIMULATION
#define SENSORS_SIMULATION           1
#endif

// TRUE si los drivers de sensores deben sintetizar lecturas.
#define SENSORS_FAKE  (SIMULATION_MODE || SENSORS_SIMULATION)

// --- Comunicaciones (fase 4 — base) ---------------------------------------
// Módulos opcionales WiFi y BLE. Por default deshabilitados — el código de
// cada uno está encerrado en #if WIFI_ENABLED / #if BLE_ENABLED para no
// agregar peso al binario hasta que se necesiten.
//
// AVISO: WiFi y BLE pueden coexistir en el ESP32-S3 (radio compartida), pero
// consumen más RAM/heap juntos. Para evitar configuraciones pesadas por
// default sólo se enciende uno a la vez en la práctica. Activarlos en
// runtime con flags = 1 antes de compilar.
#ifndef WIFI_ENABLED
#define WIFI_ENABLED                 1
#endif
#ifndef BLE_ENABLED
#define BLE_ENABLED                  0
#endif

// Servidor web de monitoreo (dashboard de SOLO LECTURA en la IP del equipo,
// puerto 80). Requiere WiFi → por default sigue a WIFI_ENABLED. El módulo
// main/web_server.c queda como stub no-op si está en 0.
#ifndef WEB_SERVER_ENABLED
#define WEB_SERVER_ENABLED           WIFI_ENABLED
#endif

// --- Telemetría a la nube (fase 4 — F1) ------------------------------------
// El equipo empuja su estado por HTTPS a un backend propio (bioorigen-web).
// El cliente lo mira desde su casa; Bio Origen monitorea toda la flota. La
// ausencia de heartbeats en el backend = equipo sin luz/internet. Requiere
// WiFi → por default sigue a WIFI_ENABLED. main/cloud_telemetry.c es no-op si
// está en 0. La identidad (dev_id/token/cloud_url) se pre-carga de fábrica en
// la partición NVS "factory"; fallback de compilación en device_identity.h.
#ifndef CLOUD_TELEMETRY_ENABLED
#define CLOUD_TELEMETRY_ENABLED      WIFI_ENABLED
#endif
#define CLOUD_PUSH_INTERVAL_S        60       // heartbeat cada 60 s
#define CLOUD_HTTP_TIMEOUT_MS        8000     // timeout del POST HTTPS
#define CLOUD_RETRY_BACKOFF_MS       15000    // espera tras un push fallido
#define CLOUD_TASK_STACK             8192     // TLS necesita stack grande
#define CLOUD_TASK_PRIO              3        // baja: no compite con control/safety
#define CLOUD_TASK_CORE              0        // protocolo en el core de UI/wifi

// SSID por default para el AP de configuración WiFi. Se puede sobrescribir
// vía nvs_config para sumar el modelo al nombre (ej. "FA10T-IND-26MTO").
#define WIFI_AP_SSID_DEFAULT         "FA10T-IND-26MTO"
#define WIFI_AP_PASS_DEFAULT         "bioorigen2026"    // ≥8 chars (WPA2 mínimo)

// Nombre de advertising BLE.
#define BLE_DEVICE_NAME_DEFAULT      "FA10T-IND-26MTO"

// GPIO_TEST_MODE=1: app_main() entra directo a un loop que cicla los GPIOs
// del header J8 (ver main/gpio_test.c) para identificar pinout con multímetro.
// El firmware NORMAL (UI, sensores, SSR, audio, etc.) NO se ejecuta mientras
// este flag esté en 1. Después de identificar el pinout poner en 0 y
// re-flashear para volver al firmware operativo.
#ifndef GPIO_TEST_MODE
#define GPIO_TEST_MODE               0
#endif

// --- PT1000 (resistor-divider ADC, not MAX31865) ----------------------------
// +3V3 → R3(2.2kΩ) → V_adc → R1(100Ω) → PT1000 → GND
//
// 2026-05-28: el operador verificó que GPIO4 NO está expuesto en el header J8
// del Waveshare ESP32-S3-Touch-LCD-3.5, así que el PT1000 se cablea a GPIO9
// (Pin 14 de J8, ADC1_CH8). GPIO9 tiene un pull-up de 10 kΩ a 3V3 onboard
// (sale al SD card MISO del Waveshare) que queda en paralelo con el R3
// (2.2 kΩ) del divisor resistivo de la PCB FA-10T:
//
//   R_eff = (2200 × 10000) / (2200 + 10000) ≈ 1803 Ω
//
// Sin compensar este pull-up el error es enorme y variable (+62 °C @ 0 °C
// real, +77 °C @ 60 °C real). Sustituyendo R_TOP=1803Ω en la fórmula del
// divisor, el error queda <0.1 °C en todo el rango 0..80 °C.
//
// HISTORICO: en la PCB FA-10T v1 el ADC estaba en GPIO11 (ADC2_CH0); ese pin
// también colisiona con SD_SCLK del Waveshare. Pasó a GPIO4 en fase 3 cuando
// se creía libre, y ahora a GPIO9 al verificarse el pinout real del header.
// El driver pt1000_adc.c resuelve unit/channel dinámicamente con
// adc_oneshot_io_to_channel(), por lo que basta cambiar el #define.
#define PIN_PT1000_ADC               9       // GPIO9 = ADC1_CH8 — J8 Pin 14
#define PT1000_R_TOP_OHMS            1803.0f // 2200 || 10000 (pull-up SD_MISO)
#define PT1000_R_SERIES_OHMS         100.0f
#define PT1000_VREF_V                3.3f
#define PT1000_R0_OHMS               1000.0f
// Callendar-Van Dusen coefficients (IEC 60751, PT1000)
#define PT1000_CVD_A                 3.9083e-3f
#define PT1000_CVD_B                 -5.775e-7f
// Fault thresholds (volts)
#define PT1000_FAULT_VOPEN_V         3.05f      // V_adc near rail → PT1000 open
#define PT1000_FAULT_VSHORT_V        0.20f      // V_adc near 0 → PT1000 shorted
#define PT1000_FAULT_TMIN_C          (-20.0f)
#define PT1000_FAULT_TMAX_C          200.0f
// Oversampling per read
#define PT1000_OVERSAMPLE_N          16

// --- ACS712-5A current sensor (turbine) -------------------------------------
// DESHABILITADO en HW actual: GPIO10 del board Waveshare es SD_MOSI con
// pull-up 10kΩ a 3V3 (R22), inutilizable como ADC (ver CONFLICTOS_PINES.md).
// TODO v2: reactivar ACS712 vía ADS1115 por I2C (sin compartir GPIO con SD).
#define ACS712_ENABLED               0
#define ACS712_MOCK_NOMINAL_A        0.45f           // valor sano publicado a la UI mientras esté inerte
#define PIN_ACS712_ADC               10              // sólo histórico — no se configura el ADC
#define ACS712_VREF_V                2.5f            // V at 0A
#define ACS712_SENS_V_PER_A          0.185f          // 185 mV/A for ACS712-5A
#define ACS712_RMS_SAMPLES           200             // 200 samples → 100ms @ 2kHz
#define ACS712_SAMPLE_INTERVAL_US    500
#define ACS712_LEARN_DURATION_MS     3000            // average for 3s on boot
#define ACS712_FAULT_RATIO           0.70f           // <70% of nominal = fault

// --- SSR 3 channels ---------------------------------------------------------
// Active HIGH through PC817C optocouplers, time-proportional period 1 s.
// NOTE: each SSR line needs an external 10kΩ pull-down on the PCB so it stays
// OFF during boot, before gpio_config() drives the pin.
//
// GPIO assignment — pinout J8 del Waveshare ESP32-S3-Touch-LCD-3.5:
//   H1 Pin 4  (ADC_IN)   → GPIO4  → J8 Pin 7
//   H1 Pin 5  (SSR_DRV)  → GPIO21 → J8           ✓ VERIFICADO (cableado real)
//   H1 Pin 6  (SSR_FAN)  → GPIO17 → J8 Pin 17
//   H1 Pin 7  (SSR_AUX)  → GPIO18 → J8 Pin 9
//   H1 Pin 9  (I2C_SDA)  → GPIO8  → J8 Pin 28
//   H1 Pin 10 (I2C_SCL)  → GPIO7  → J8 Pin 26
//
// 2026-05-30: GPIO21 CONFIRMADO por Emilio como el cableado real del SSR_DRV
// en este display — sin más incertidumbre. (Histórico: el 2026-05-28 se
// reportó por error GPIO23 en J8 Pin 11, pero el ESP32-S3 NO tiene GPIO22..25
// físicamente —datasheet tabla 2-1: pads válidos 0..21 y 26..48— y ESP-IDF
// aborta con "GPIO_PIN mask error" al gpio_config() sobre GPIO23.)
#define PIN_SSR_DRV                  21    // heaters    (J8 — GPIO21 verificado)
#define PIN_SSR_FAN                  17    // turbinas   (J8 pin 17)
#define PIN_SSR_AUX                  18    // extractor  (J8 pin 9)
#define SSR_PERIOD_MS                1000
#define SSR_ACTIVE_HIGH              true
#define SSR_TICK_INTERVAL_MS         10

// --- Consumo energético (estimación para mostrar kWh) -----------------------
// Potencia POR MÓDULO (una resistencia + una turbina). El firmware integra el
// consumo de UN módulo (session_energy_wh / session_fan_on_s) y tanto el LCD
// como la nube lo multiplican por num_modulos.
//   - Resistencia: 2000 W a 220 V (carga resistiva, FP≈1).
//   - Turbina:     motor de polo de sombra 220 V × 0,40 A = 88 VA ≈ 88 W.
//                  (potencia aparente; la activa es algo menor por el FP bajo
//                  del shaded-pole, pero es marginal frente a los 2000 W).
#define RES_WATTS_PER_MODULE         2000.0f
#define FAN_WATTS_PER_MODULE         88.0f

// --- I2C bus (shared SHT31 + PCF85063 RTC) ----------------------------------
#define PIN_I2C_SCL                  7
#define PIN_I2C_SDA                  8
#define I2C_BUS_PORT                 I2C_NUM_0
#define I2C_BUS_CLK_HZ               400000

#define SHT31_I2C_ADDR               0x44

// --- Task periods (ms) ------------------------------------------------------
#define SENSOR_TASK_PERIOD_MS        100      // PT1000 @10Hz; ACS/SHT31 sub-rated
#define CONTROL_TASK_PERIOD_MS       200      // PID @5Hz
#define UI_TASK_PERIOD_MS            2000     // serial print every 2s
#define WATCHDOG_TASK_PERIOD_MS      500

// --- Task stacks / priorities ----------------------------------------------
#define SENSOR_TASK_STACK            4096
#define CONTROL_TASK_STACK           4096
#define UI_TASK_STACK                4096
#define WATCHDOG_TASK_STACK          3072

#define SENSOR_TASK_PRIO             6
#define CONTROL_TASK_PRIO            7
#define WATCHDOG_TASK_PRIO           8
#define UI_TASK_PRIO                 4

#define SENSOR_TASK_CORE             1
#define CONTROL_TASK_CORE            1
#define WATCHDOG_TASK_CORE           0
#define UI_TASK_CORE                 0

// --- Operating envelope -----------------------------------------------------
#define OPERATING_TEMP_MIN_C         20.0f
#define OPERATING_TEMP_MAX_C         80.0f

// --- Warm-up --------------------------------------------------------------
// El "tiempo de proceso" arranca cuando T_actual >= setpoint - WARMUP_TOLERANCE.
// 0.5 °C deja un margen mínimo para evitar quedar atrapado por la asíntota
// del primer orden sin saltar antes de tiempo.
#define WARMUP_TOLERANCE_C           0.5f

// --- Safety / runaway -------------------------------------------------------
// Hard absolute emergency: any T above this latches OVERTEMP and cuts all SSRs.
#define SAFETY_TEMP_MAX_C            85.0f
// Runaway: with SSR OFF, T must NOT rise faster than RUNAWAY_DT_C in WINDOW_S.
// (Detects shorted SSR, stuck heater, etc.)
#define SAFETY_RUNAWAY_DT_C          5.0f
#define SAFETY_RUNAWAY_WINDOW_S      60.0f
#define SAFETY_RUNAWAY_DUTY_THR      0.05f    // observe when duty ≈ 0
#define SAFETY_WDT_TIMEOUT_S         5
#define SAFETY_HYSTERESIS_C          5.0f
#define SAFETY_RECOVERY_RAMP_S       8.0f

// --- Reset / acknowledge button --------------------------------------------
#define PIN_BTN_RESET                0      // ESP32-S3 BOOT button
#define BTN_RESET_HOLD_MS            1500
