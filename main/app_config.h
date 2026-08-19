#pragma once

#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_types.h"

// --- Build mode -------------------------------------------------------------
// SIMULATION_MODE=1: TODO simulado (incluye display/touch/audio fake). Útil
// para correr el firmware en host o sin hardware conectado.
#ifndef SIMULATION_MODE
#define SIMULATION_MODE              0
#endif

// SENSORS_SIMULATION=1: drivers de sensores (DS18B20, SHT31, ACS712) devuelven
// valores sintéticos plausibles, pero el display + touch + audio + bus I2C
// siguen funcionando contra hardware real. Sirve para probar la UI en el
// equipo cuando todavía no está conectada la PCB FA-10T con los sensores
// físicos. La planta simulada calienta hacia el setpoint con tau ≈ 120 s, la
// humedad baja de 70%→25% en 12 min, la corriente de turbina queda fija en
// nominal. Los SSR SÍ se siguen controlando (output real al GPIO) — apagar
// si la PCB de potencia está conectada y no querés energizar nada.
#ifndef SENSORS_SIMULATION
#define SENSORS_SIMULATION           0
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

// --- DS18B20 (1-Wire) — sensor de temperatura de la placa v3 ----------------
// v3 (2026-08): la temperatura pasa a sensores DIGITALES DS18B20 en un bus
// 1-Wire (sin ADC → sin el ruido de ±6 °C que tenía el PT1000). Reemplaza al
// PT1000. Pin de datos = GPIO40 (H1 pin 5 = pin 11 del header del Waveshare).
// ¡GPIO4 NO está expuesto en el header → NO usar! Pull-up 4.7k a 3V3 en la PCB.
// Sondas estancas de acero con cable siliconado (aguantan el vapor + calor).
// MODELO 2 SONDAS: una ARRIBA y otra ABAJO del equipo (separadas ~1,6 m). El
// PID controla sobre el PROMEDIO de ambas; la seguridad usa la MÁS CALIENTE.
// Si una falla, se sigue con la otra; solo si fallan las dos → SENSOR_FAULT.
#define PIN_ONEWIRE                  40      // GPIO40 = H1 pin 5 (1-Wire DQ)
#define DS18B20_RESOLUTION_BITS      12      // 12 bits = 0.0625 °C (~750 ms conv)
#define DS18B20_MAX_SENSORS          4       // en este modelo se usan 2 (multidrop)
#define DS18B20_FAULT_TMIN_C         (-20.0f)
#define DS18B20_FAULT_TMAX_C         200.0f
// El DS18B20 es DIGITAL: no tiene el ruido del ADC del PT1000, así que el
// filtro puede ser mucho más rápido (menos lag para el PID). Se conserva la
// mediana por si aparece algún glitch del bus 1-Wire.
#define DS18B20_FILTER_TAU_S         1.0f
#define DS18B20_MEDIAN_N             5       // ventana de mediana (anti-glitch de bus)
// Debounce de falla: cuántos ciclos de sensor_task (100 ms) seguidos tienen que
// venir en falla antes de declarar SENSOR_FAULT y cortar las salidas. Evita que
// un glitch aislado del bus apague el calefactor. 5 ciclos = 0,5 s.
#define SENSOR_FAULT_DEBOUNCE_N      5

// --- PT1000 — ELIMINADO en la placa v3 ---------------------------------------
// La temperatura pasó a DS18B20 (1-Wire, arriba). El driver pt1000_adc.c ya no
// se compila (se quitó de main/CMakeLists.txt) y sus #define desaparecieron.
// Motivo del cambio: el divisor resistivo iba a un GPIO con pull-up del Waveshare
// (ruido de ±6 °C en el ADC) y el ADS1115 no se conseguía en el país.
// El histórico completo está en git y en la doc del proyecto.

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
// Control time-proportional, período 1 s, vía optoacopladores PC817C en la
// placa FA-10T. Los módulos de potencia son SSR a TRIAC con driver MOC3041
// (opto zero-cross). La entrada "12VCC" del módulo es la SEÑAL DE DISPARO
// ACTIVE-HIGH (12 V = ON, 0 V = OFF) — NO una alimentación permanente; el LED
// del módulo indica esa entrada energizada (= salida ON). La FA-10T conmuta
// esos 12 V por canal con el PC817 comandado por el GPIO (cadena active-high:
// GPIO alto → PC817 → 12 V → MOC3041 → ON) y un pull-down en la línea del GPIO
// que mantiene OFF durante el boot. Por eso SSR_ACTIVE_HIGH = true.
// 2026-06-03: "siempre encendido" en banco = los 12VCC quedaron a 12 V fijos
// (deben ir a la salida CONMUTADA de la FA-10T, no a 12 V permanente).
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
// --- PLACA v3 (2026-08): cadena de salida NUEVA -----------------------------
// Los pines NO cambian (GPIO21/17/18), pero la electrónica sí:
//   GPIO → ULN2803 (driver, low-side) → carga con el otro extremo a +12V.
//   El ULN sinkea a GND cuando el GPIO está en ALTO → sigue siendo ACTIVE-HIGH
//   (GPIO alto = carga ON), así que SSR_ACTIVE_HIGH = true se mantiene.
//   DRV (GPIO21) → SSR-3 D38120 trifásico (RESIST)   — time-proportional (PID)
//   FAN (GPIO17) → bobina relé DIN 12VDC (turbinas)   — on/off
//   AUX (GPIO18) → bobina relé DIN 12VDC (extractor)  — on/off por humedad
// Conector display H1: pin6=DRV(GPIO21), pin7=FAN(GPIO17), pin8=AUX(GPIO18).
#define PIN_SSR_DRV                  21    // RESIST → SSR-3 D38120 (H1 pin 6)
#define PIN_SSR_FAN                  17    // FAN    → relé 12VDC   (H1 pin 7)
#define PIN_SSR_AUX                  18    // AUX    → relé 12VDC   (H1 pin 8)
#define SSR_PERIOD_MS                1000
#define SSR_ACTIVE_HIGH              true    // cadena active-high (GPIO→ULN2803→carga)
#define SSR_TICK_INTERVAL_MS         10

// --- Control del extractor (AUX) por humedad --------------------------------
// v3: la salida AUX maneja el EXTRACTOR (relé 12VDC → 220V) para sacar el aire
// húmedo de la cámara. Control ON/OFF con HISTÉRESIS + ANTI-CYCLING + FAIL-SAFE.
//   - Enciende si RH sube a EXTRACTOR_RH_ON_PCT.
//   - Apaga si RH baja a EXTRACTOR_RH_OFF_PCT (la banda evita el "castañeteo").
//   - Tiempo mínimo en cada estado (no arranca/para el motor a cada rato).
//   - Si el sensor de humedad falla → EXTRAE igual (evita acumular vapor).
// Solo actúa con sesión activa; fuera de proceso el extractor queda apagado.
#define EXTRACTOR_RH_ON_PCT          55.0f   // %RH: enciende al subir a este valor
#define EXTRACTOR_RH_OFF_PCT         47.0f   // %RH: apaga al bajar a este valor
#define EXTRACTOR_MIN_ON_S           60.0f   // s mínimos encendido (anti-cycling)
#define EXTRACTOR_MIN_OFF_S          60.0f   // s mínimos apagado

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
// v3 (2026-08): bus a 100 kHz. En la placa v3 los SHT31 van REMOTOS en la
// cámara por cable (I2C no tolera cable largo a 400 kHz → bajamos a 100 kHz).
#define I2C_BUS_CLK_HZ               100000

// Dos SHT31 en el mismo bus (módulos GY-SHT31-D):
//   U5 = 0x44 (pad AD a GND, default) · U6 = 0x45 (puente AD→VIN en el módulo)
#define SHT31_I2C_ADDR               0x44
#define SHT31_I2C_ADDR2              0x45

// --- SHT31 heater interno (anti-condensación) -------------------------------
// El SHT31 tiene un calentador interno para evaporar condensación de la ventana.
// Si la RH queda muy alta de forma sostenida (típico de condensación, no de
// proceso), se pulsa el heater unos segundos y después se deja enfriar antes de
// volver a leer (durante el pulso/enfriado las lecturas se descartan). El
// control de humedad mantiene el último valor válido mientras tanto.
#define SHT31_HEATER_ENABLE          1
#define SHT31_HEATER_RH_TRIGGER      97.0f   // %RH: por encima suele ser condensación
#define SHT31_HEATER_ON_S            5.0f    // s con el heater encendido
#define SHT31_HEATER_SETTLE_S        10.0f   // s de enfriamiento antes de re-leer
#define SHT31_HEATER_MIN_INTERVAL_S  300.0f  // s mínimos entre pulsos

// --- Task periods (ms) ------------------------------------------------------
#define SENSOR_TASK_PERIOD_MS        100      // temperatura @10Hz; ACS/SHT31 sub-rated
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

// --- Auto-stop por humedad (modo automático) --------------------------------
// Si la sesión (manual o receta) tiene un "humedad objetivo" (>0), el proceso
// corre HASTA alcanzar esa humedad de forma SOSTENIDA (después de un tiempo
// mínimo), SIN depender del tiempo programado: ese tiempo pasa a ser sólo una
// guía. Si el objetivo es 0, la sesión se completa por tiempo (clásico).
// Salvaguardas para no correr indefinidamente:
//   - HUM_MODE_MAX_RUN_S: tope absoluto de seguridad (la humedad nunca baja).
//   - si el sensor de humedad queda en falla sostenida Y ya pasó el tiempo
//     programado, se completa por tiempo (respaldo).
// Parámetros acá (no en NVS, para no invalidar la config guardada):
#define HUM_AUTOSTOP_SUSTAIN_S       180      // humedad <= objetivo sostenida N s antes de cortar
#define HUM_AUTOSTOP_MIN_RUN_S       900      // no auto-parar antes de N s (post-calentamiento)
#define HUM_AUTOSTOP_FAULT_TO_S      30       // falla del sensor de humedad sostenida -> respaldo/aviso
#define HUM_MODE_MAX_RUN_S           86400    // tope absoluto de seguridad en modo humedad (24 h)
#define HUM_TARGET_MIN_PCT           3.0f     // objetivo mínimo editable en la UI
#define HUM_TARGET_MAX_PCT           40.0f    // objetivo máximo editable en la UI

// --- Enfriamiento post-proceso (cool-down) ----------------------------------
// Al COMPLETAR una sesión (por tiempo o por humedad), el calefactor se apaga
// pero las turbinas siguen ventilando para enfriar el producto y la cámara.
// Corta cuando la temperatura baja al objetivo (COOLDOWN_TEMP_C) O al cumplirse
// el tope de tiempo (lo que ocurra primero). El tope es una SALVAGUARDA: si el
// ambiente está caluroso y nunca se llega a esa temperatura, evita que la
// turbina quede ventilando indefinidamente.
#define COOLDOWN_TEMP_C              30.0f    // corta el enfriamiento al bajar a esta T
#define COOLDOWN_DURATION_S          1800     // tope de seguridad: 30 min máx de ventilación

// --- Safety / runaway -------------------------------------------------------
// Hard absolute emergency: any T above this latches OVERTEMP and cuts all SSRs.
// Este es el límite del sensor de CONTROL (en el aire de la cámara).
#define SAFETY_TEMP_MAX_C            85.0f
// MODELO 2 SONDAS (2026-08): el PID controla sobre el PROMEDIO de las 2 sondas
// DS18B20 (arriba/abajo), pero la seguridad se evalúa sobre la MÁXIMA de las
// dos: si cualquiera supera este valor → OVERTEMP latcheado y corta el SSR por
// software. La seguridad de HARDWARE la da un TERMOSTATO MECÁNICO de 95°C en
// serie con las resistencias, independiente del firmware (ya NO se usa una
// sonda DS18B20 de seguridad).
#define SAFETY_LIMIT_TEMP_C          85.0f
// BANCO DE PRUEBA: 1 = desactiva el detector de RUNAWAY para poder ejercitar
// las salidas RESIST/FAN/AUX sin calefactor (sensor en agua caliente sube la T
// con el SSR en OFF y dispararía un runaway falso). ¡PONER EN 0 antes de
// conectar el calefactor real! Con 1 NO hay protección contra SSR pegado.
#define SAFETY_BENCH_TEST            1
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
