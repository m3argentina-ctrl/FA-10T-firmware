# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Firmware ESP-IDF del controlador táctil de deshidratadores industriales Bio Origen. Proyecto: **"CONTROL TACTIL ESP32"**. Identidad interna del código: **FA10T** (deviceId `FA10T-0001`, repo `FA-10T-firmware`, namespaces NVS `fa10t_*`, `project(fa10t_firmware)`). Código y comentarios en español.

Hardware real (placa v3): ESP32-S3 + Waveshare ESP32-S3-Touch-LCD-3.5 (ST7796 + FT6336 + TCA9554), **2× DS18B20** (temperatura, 1-Wire), **1× SHT31** (humedad, I2C, 0x44) y 3 salidas vía ULN2803: DRV → SSR-3 trifásico (resistencias), FAN → relé DIN 12 VDC (turbinas, 220 V / 35 W c/u), AUX → relé DIN 12 VDC (extractor). **No hay sensor de corriente**: `ACS712_ENABLED=0` devuelve un valor fijo, así que la falla de turbina y la detección de relé pegado quedan inertes.

**Pinout de la placa (fuente de verdad):** `PCB_v3\Guia_Cableado_V3.xlsx`, en esta misma carpeta. Desde el 2026-09-15 esta carpeta es la única de trabajo del proyecto: `PCB_v3\`, `docs\` (manuales y sus fuentes) e `Imagenes\` viven acá pero no se versionan; `D:\BIOORIGEN\DESHIDRATADORES\CONTROL TACTIL ESP32\` y `E:\CONTROL TACTIL ESP32\` son copias de respaldo (ver `_INDICE_PROYECTO.md`). H1 → header del Waveshare: SCL GPIO7 → pin 26, SDA GPIO8 → pin 28, 1-Wire GPIO40 → pin 11, DRV GPIO21 → pin 5, FAN GPIO17 → pin 16, AUX GPIO18 → pin 18.

## Build / flash (Windows, ESP-IDF v6.0.1)

El SDK y las tools NO están en el PATH global y `python` resuelve al stub de la Microsoft Store (rompe `export.ps1`). Usar el python del venv EXPLÍCITO, todo en UNA llamada de PowerShell:

```powershell
$repo = "C:\Users\Emilio\FA-10T-firmware"
. "$repo\activate-idf.ps1"          # setea IDF_PATH (SDK en D:) + IDF_TOOLS_PATH
$py = "C:\Users\Emilio\.espressif\python_env\idf6.0_py3.14_env\Scripts\python.exe"
& $py "$env:IDF_PATH\tools\idf.py" -C $repo build          # o: -p COM3 flash  /  -p COM3 monitor
Write-Output "EXITCODE=$LASTEXITCODE"
```

- Puertos: **COM3 = IND30S-0001**, **COM4 = FA10T-0002**. Sin cable: OTA por WiFi en `http://<IP>/update` subiendo `build/fa10t_firmware.bin` (rechaza si hay proceso o autotune en marcha). Pide el PIN de servicio: el formulario lo manda en el header `X-Pin`; desde la PC `curl -X POST -H "X-Pin: <PIN>" -H "Content-Type: application/octet-stream" --data-binary @build/fa10t_firmware.bin http://<IP>/update`. 5 PIN incorrectos seguidos bloquean la OTA 15 min.
- Build incremental ~1-2 min; build limpio ~3 min. Correr con `run_in_background` y leer el `.output`.
- No hay tests. La verificación es en hardware (flash + serial) o con `SIMULATION_MODE=1`.

## Reglas críticas

- **NO mover el repo a rutas largas.** DEBE quedar en la ruta CORTA `C:\Users\Emilio\FA-10T-firmware`. Con rutas profundas (p.ej. dentro de la carpeta del proyecto en `D:\...`), el paso `ldgen` del linkeo supera el límite de línea de comando de Windows (~32 KB) y falla con *"CreateProcess failed"*.
- **Commit/flashear SÓLO cuando el usuario lo pide explícitamente.** No commitear ni flashear firmware de motu propio.
- **Nunca commitear identidad/secretos.** Gitignorados: `main/device_identity.h` (el versionado es `device_identity.example.h`), `tools/provision/*.csv`, `tools/provision/*.bin`. Los tokens de dispositivo y la `DATABASE_URL` no van al repo.
- **`SAFETY_BENCH_TEST` (app_config.h) DEBE estar en 0 con calefactor real** — con 1 se desactiva el detector de runaway (sin protección contra SSR pegado). Sólo se pone en 1 para ejercitar salidas en banco sin calefactor.
- **`dashboard.html` va embebido en el binario** (`EMBED_TXTFILES` en CMakeLists) → editarlo NO tiene efecto hasta recompilar y reflashear.
- `README.md` y `CONFLICTOS_PINES.md` son de la placa v1 (PT1000/MAX31865, PC817, ACS712 en GPIO10): **obsoletos**, no usarlos para razonar sobre el hardware. `main/display_pins.h` es el pinout INTERNO del Waveshare (LCD, touch, expansor); el de la placa está en `PCB_v3\Guia_Cableado_V3.xlsx` (no versionado, ver arriba).

## Arquitectura (lo que requiere leer varios archivos)

**Estado compartido + máquina de estados.** Todo el estado vivo está en un único `app_state_t` (`app_state.h`) detrás de un mutex. Patrón obligatorio: `app_state_lock()` → copiar/leer/mutar → `app_state_unlock()`; **decidir y hacer trabajo lento (audio, NVS, logs) FUERA del lock**. Dos ejes de estado: `op_mode` (IDLE / MANUAL / PROGRAMS) y `run_state` (IDLE / CONFIGURING / RUNNING / PAUSED / COMPLETED / ALARM).

**Tareas FreeRTOS** (`main/tasks/`):
- `sensor_task` — lee DS18B20 (promedio para control, máxima para seguridad) + SHT31, publica `sensor_sample_t` a una cola.
- `control_task` — corazón del sistema: corre el PID, aplica los SSR (time-proportional vía `ssr3ch`), y en cada ciclo llama a los "ticks" que mutan `app_state` (ver abajo), más `safety_evaluate()`.
- `ui_task` — LVGL 8.4. Las pantallas (`main/screens/`) se construyen una vez y se refrescan en `LV_EVENT_SCREEN_LOAD_START`; los modales van en `lv_layer_top()`. AREA TECNICA no tiene botón en el menú (el cliente no debe verla): se entra manteniendo apretado 5 s el isologo del panel izquierdo (`TECNICA_HOLD_MS` en `ui_common.c`) y después pide el PIN de servicio; salir de esa pantalla por cualquier camino cierra la sesión. El manual del cliente no la documenta.
- `watchdog_task` — Task Watchdog Timer + chequeos de seguridad.

**Módulos "tick"** (llamados desde `control_task` cada ciclo, mutan `app_state`): `modo_manual_tick` / `programa_tick` (avance de tiempo/etapas de la sesión), `humidity_autostop_tick` (completa por humedad objetivo), `cooldown_tick` (ventilación post-proceso), `extractor_tick` (AUX por humedad), `autotune_step` (sólo en reposo: relé con sesgo sobre las resistencias; no toca `run_state`, por eso la OTA y los comandos remotos lo chequean aparte). Todos leen bajo lock y revalidan el estado antes de commitear un cambio de `run_state`.

**Lógica de sesión.** Si `humidity_target == 0` la sesión completa por tiempo; si es `>0` corre hasta alcanzar la humedad (el tiempo es guía), con topes de seguridad (`HUM_MODE_MAX_RUN_S`, respaldo por falla de sensor). Al COMPLETAR entra en enfriamiento (`cooling_active`): ventila hasta `COOLDOWN_TEMP_C` o el tope de tiempo. Una alarma (`SAFETY_TRIP_MASK`) cancela la sesión en curso en el mismo ciclo (`control_task`: `op_mode` → IDLE, SP 0); al reconocerla el equipo queda en reposo y el proceso se inicia de nuevo a mano.

**Seguridad en capas:** runaway (T sube con SSR en OFF) → OVERTEMP latcheado si la sonda de aire más caliente supera `SAFETY_LIMIT_TEMP_C` (85°C) → sonda opcional de resistencias (3ª DS18B20 asignada por ROM en AREA TECNICA → SONDAS, fuera del promedio del PID) con `HEATER_LIMIT_TEMP_C` (90°C) y alarma si no lee con calor pedido → termostato mecánico 95°C (hardware, fuera del firmware).

**Persistencia (NVS).** Config (`fa10t_config_t`: PID, calibración) en `components/nvs_config`. Identidad editable (modelo/serie/pin/módulos) en namespace `fa10t_id`. Recetas de programas en `fa10t_prog` (se siembran recetas de fábrica en equipos nuevos vía flag `seed_ver`). Contadores de telemetría + snapshot de recuperación (resume tras corte de luz). **La provisión de nube (dev_id/token) vive en una partición NVS aparte `factory`** (ver `partitions.csv`) que `nvs_flash_erase()` NO toca — por eso el RESET TOTAL de fábrica la conserva. En esa misma partición (namespace `fa10t_tune`, `tune_store.c`) van el PID del autotune y la ROM de la sonda de resistencias, así también sobreviven al RESET TOTAL (se pierden si se re-provisiona, porque `provision.ps1` reescribe la partición). Con ganancias de autotune el PID usa integración condicional (`kt=0`): con su Ki chico, `kt=Ki` no frena el windup.

**Comunicaciones** (todas bajo `#if WIFI_ENABLED` en `app_config.h`): servidor web local (`web_server.c`: `/`, `/api/status`, `/api/events`, `/api/diag` = scan del bus I2C + lectura del SHT31), OTA por WiFi (`ota_update.c`: `POST /update`), y telemetría a la nube (`cloud_telemetry.c`: HTTPS a bioorigen-web, identidad de la partición `factory`).

## Configuración clave (`main/app_config.h`)

Toggles de compilación: `SIMULATION_MODE` (1 = todo mock, dev sin HW), `SENSORS_SIMULATION` (1 = sólo sensores fake), `WIFI_ENABLED` / `WEB_SERVER_ENABLED` / `CLOUD_TELEMETRY_ENABLED`, `SAFETY_BENCH_TEST`. Umbrales ajustables: `SAFETY_LIMIT_TEMP_C`, `SAFETY_RUNAWAY_*`, `HUM_AUTOSTOP_*` / `HUM_MODE_MAX_RUN_S`, `COOLDOWN_TEMP_C` / `COOLDOWN_DURATION_S`, `WARMUP_TOLERANCE_C`.

## Gotchas del entorno

- **`-Werror` activo.** Structs vacíos de componentes (p.ej. `ds18b20_config_t`) inicializar con `{ }`, no `{ 0 }`.
- **APIs de componentes que bloquean:** verificar antes de asumir no-bloqueante. `ds18b20`/`onewire_bus` hacen `vTaskDelay` internos → la lectura corre en tarea propia en `ds18b20_bus.c` (no bloquear `sensor_task`).
- **Acumuladores de tiempo:** sumar `dt_s` (float ~0.2s) a un `uint32` trunca a 0; usar un acumulador `float` fraccional separado (patrón en `modo_manual.c`/`programa.c`).
- Repo con finales de línea **LF**; en checkout Windows git avisa "LF will be replaced by CRLF" (inofensivo).
