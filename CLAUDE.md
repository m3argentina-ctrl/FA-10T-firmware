# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Firmware ESP-IDF del controlador táctil de deshidratadores industriales Bio Origen. Proyecto: **"CONTROL TACTIL ESP32"**. Identidad interna del código: **FA10T** (deviceId `FA10T-0001`, repo `FA-10T-firmware`, namespaces NVS `fa10t_*`, `project(fa10t_firmware)`). Código y comentarios en español.

Hardware real (placa v3): ESP32-S3 + Waveshare ESP32-S3-Touch-LCD-3.5 (ST7796 + FT6336 + TCA9554), **2× DS18B20** (temperatura, 1-Wire), **1× SHT31** (humedad, I2C), **SSR 3 canales** (DRV resistencias / FAN turbina / AUX extractor).

## Build / flash (Windows, ESP-IDF v6.0.1)

El SDK y las tools NO están en el PATH global y `python` resuelve al stub de la Microsoft Store (rompe `export.ps1`). Usar el python del venv EXPLÍCITO, todo en UNA llamada de PowerShell:

```powershell
$repo = "C:\Users\Emilio\FA-10T-firmware"
. "$repo\activate-idf.ps1"          # setea IDF_PATH (SDK en D:) + IDF_TOOLS_PATH
$py = "C:\Users\Emilio\.espressif\python_env\idf6.0_py3.14_env\Scripts\python.exe"
& $py "$env:IDF_PATH\tools\idf.py" -C $repo build          # o: -p COM3 flash  /  -p COM3 monitor
Write-Output "EXITCODE=$LASTEXITCODE"
```

- Puertos: **COM3 = FA10T-0001**, **COM4 = FA10T-0002**.
- Build incremental ~1-2 min; build limpio ~3 min. Correr con `run_in_background` y leer el `.output`.
- No hay tests. La verificación es en hardware (flash + serial) o con `SIMULATION_MODE=1`.

## Reglas críticas

- **NO mover el repo a rutas largas.** DEBE quedar en la ruta CORTA `C:\Users\Emilio\FA-10T-firmware`. Con rutas profundas (p.ej. dentro de la carpeta del proyecto en `D:\...`), el paso `ldgen` del linkeo supera el límite de línea de comando de Windows (~32 KB) y falla con *"CreateProcess failed"*.
- **Commit/flashear SÓLO cuando el usuario lo pide explícitamente.** No commitear ni flashear firmware de motu propio.
- **Nunca commitear identidad/secretos.** Gitignorados: `main/device_identity.h` (el versionado es `device_identity.example.h`), `tools/provision/*.csv`, `tools/provision/*.bin`. Los tokens de dispositivo y la `DATABASE_URL` no van al repo.
- **`SAFETY_BENCH_TEST` (app_config.h) DEBE estar en 0 con calefactor real** — con 1 se desactiva el detector de runaway (sin protección contra SSR pegado). Sólo se pone en 1 para ejercitar salidas en banco sin calefactor.
- **`dashboard.html` va embebido en el binario** (`EMBED_TXTFILES` en CMakeLists) → editarlo NO tiene efecto hasta recompilar y reflashear.
- `README.md` está **obsoleto** (describe la placa v1 con PT1000/MAX31865 SPI y componentes ya borrados). El pinout real está en `main/display_pins.h`; el mapeo del conector H1↔display está en una planilla externa (`Guia_Cableado_V3.xlsx`), no en el repo.

## Arquitectura (lo que requiere leer varios archivos)

**Estado compartido + máquina de estados.** Todo el estado vivo está en un único `app_state_t` (`app_state.h`) detrás de un mutex. Patrón obligatorio: `app_state_lock()` → copiar/leer/mutar → `app_state_unlock()`; **decidir y hacer trabajo lento (audio, NVS, logs) FUERA del lock**. Dos ejes de estado: `op_mode` (IDLE / MANUAL / PROGRAMS) y `run_state` (IDLE / CONFIGURING / RUNNING / PAUSED / COMPLETED / ALARM).

**Tareas FreeRTOS** (`main/tasks/`):
- `sensor_task` — lee DS18B20 (promedio para control, máxima para seguridad) + SHT31, publica `sensor_sample_t` a una cola.
- `control_task` — corazón del sistema: corre el PID, aplica los SSR (time-proportional vía `ssr3ch`), y en cada ciclo llama a los "ticks" que mutan `app_state` (ver abajo), más `safety_evaluate()`.
- `ui_task` — LVGL 8.4. Las pantallas (`main/screens/`) se construyen una vez y se refrescan en `LV_EVENT_SCREEN_LOAD_START`; los modales van en `lv_layer_top()`.
- `watchdog_task` — Task Watchdog Timer + chequeos de seguridad.

**Módulos "tick"** (llamados desde `control_task` cada ciclo, mutan `app_state`): `modo_manual_tick` / `programa_tick` (avance de tiempo/etapas de la sesión), `humidity_autostop_tick` (completa por humedad objetivo), `cooldown_tick` (ventilación post-proceso), `extractor_tick` (AUX por humedad). Todos leen bajo lock y revalidan el estado antes de commitear un cambio de `run_state`.

**Lógica de sesión.** Si `humidity_target == 0` la sesión completa por tiempo; si es `>0` corre hasta alcanzar la humedad (el tiempo es guía), con topes de seguridad (`HUM_MODE_MAX_RUN_S`, respaldo por falla de sensor). Al COMPLETAR entra en enfriamiento (`cooling_active`): ventila hasta `COOLDOWN_TEMP_C` o el tope de tiempo.

**Seguridad en capas:** runaway (T sube con SSR en OFF) → OVERTEMP latcheado si cualquier sonda supera `SAFETY_LIMIT_TEMP_C` (85°C) → termostato mecánico 95°C (hardware, fuera del firmware).

**Persistencia (NVS).** Config (`fa10t_config_t`: PID, calibración) en `components/nvs_config`. Identidad editable (modelo/serie/pin/módulos) en namespace `fa10t_id`. Recetas de programas en `fa10t_prog` (se siembran recetas de fábrica en equipos nuevos vía flag `seed_ver`). Contadores de telemetría + snapshot de recuperación (resume tras corte de luz). **La provisión de nube (dev_id/token) vive en una partición NVS aparte `factory`** (ver `partitions.csv`) que `nvs_flash_erase()` NO toca — por eso el RESET TOTAL de fábrica la conserva.

**Comunicaciones** (todas bajo `#if WIFI_ENABLED` en `app_config.h`): servidor web local (`web_server.c`: `/`, `/api/status`, `/api/events`), OTA por WiFi (`ota_update.c`: `POST /update`), y telemetría a la nube (`cloud_telemetry.c`: HTTPS a bioorigen-web, identidad de la partición `factory`).

## Configuración clave (`main/app_config.h`)

Toggles de compilación: `SIMULATION_MODE` (1 = todo mock, dev sin HW), `SENSORS_SIMULATION` (1 = sólo sensores fake), `WIFI_ENABLED` / `WEB_SERVER_ENABLED` / `CLOUD_TELEMETRY_ENABLED`, `SAFETY_BENCH_TEST`. Umbrales ajustables: `SAFETY_LIMIT_TEMP_C`, `SAFETY_RUNAWAY_*`, `HUM_AUTOSTOP_*` / `HUM_MODE_MAX_RUN_S`, `COOLDOWN_TEMP_C` / `COOLDOWN_DURATION_S`, `WARMUP_TOLERANCE_C`.

## Gotchas del entorno

- **`-Werror` activo.** Structs vacíos de componentes (p.ej. `ds18b20_config_t`) inicializar con `{ }`, no `{ 0 }`.
- **APIs de componentes que bloquean:** verificar antes de asumir no-bloqueante. `ds18b20`/`onewire_bus` hacen `vTaskDelay` internos → la lectura corre en tarea propia en `ds18b20_bus.c` (no bloquear `sensor_task`).
- **Acumuladores de tiempo:** sumar `dt_s` (float ~0.2s) a un `uint32` trunca a 0; usar un acumulador `float` fraccional separado (patrón en `modo_manual.c`/`programa.c`).
- Repo con finales de línea **LF**; en checkout Windows git avisa "LF will be replaced by CRLF" (inofensivo).
