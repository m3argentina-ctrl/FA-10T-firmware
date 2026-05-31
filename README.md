# Control ESP32-S3 v3.0 — Industrial Thermal Controller

Firmware ESP-IDF para controlador térmico industrial **Control ESP32-S3 v3.0** sobre **ESP32-S3**.

## Características principales

- Lectura de temperatura **PT1000** vía **MAX31865** (SPI, Callendar–Van Dusen).
- Lazo de control **PID** con anti-windup y limitación de salida.
- Actuación sobre **SSR** mediante PWM lento (time-proportional).
- Interfaz gráfica en **TFT** con **LVGL**.
- Arquitectura **FreeRTOS** multi-tarea: `sensor`, `control`, `ui`, `watchdog`.
- **Task Watchdog Timer** y detección de **runaway térmico** / sensor abierto.
- Persistencia de configuración en **NVS** (PID, setpoint, calibración).

## Arquitectura

```
+-------------+    queue    +--------------+    +--------+
| sensor_task |───────────▶ | control_task |───▶|  SSR   |
| (MAX31865)  |             |   (PID)      |    | driver |
+-------------+             +--------------+    +--------+
       │                           │
       │ shared state (mutex)      │
       ▼                           ▼
+----------------+         +---------------+
|    ui_task     |         | watchdog_task |
|     (LVGL)     |         | safety + TWDT |
+----------------+         +---------------+
```

## Estructura del proyecto

```
FA-10T-firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── main.c
│   ├── app_config.h
│   ├── app_state.{c,h}
│   └── tasks/
│       ├── sensor_task.{c,h}
│       ├── control_task.{c,h}
│       ├── ui_task.{c,h}
│       └── watchdog_task.{c,h}
└── components/
    ├── max31865/
    ├── pid_controller/
    ├── ssr_driver/
    ├── display/
    ├── nvs_config/
    └── safety/
```

## Pinout (ESP32-S3, por defecto)

| Función             | GPIO |
|---------------------|------|
| MAX31865 SPI MOSI   | 11   |
| MAX31865 SPI MISO   | 13   |
| MAX31865 SPI CLK    | 12   |
| MAX31865 CS         | 10   |
| MAX31865 DRDY       |  9   |
| SSR control         | 21   |
| TFT MOSI            | 38   |
| TFT MISO            | 40   |
| TFT CLK             | 39   |
| TFT CS              | 41   |
| TFT DC              | 42   |
| TFT RST             | 45   |
| TFT BL              | 48   |

Los pines son configurables en `main/app_config.h`.

## Build

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

## Licencia

Propietario — M3 Argentina.
