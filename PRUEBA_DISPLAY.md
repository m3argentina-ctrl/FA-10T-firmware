# PRUEBA_DISPLAY.md — Primer encendido del Waveshare ESP32-S3-Touch-LCD-3.5

**Firmware:** FA-10T v3.0, fase 3 (integración display)
**Hardware:** Waveshare ESP32-S3-Touch-LCD-3.5 (sin sufijo "B")
**Build verificado:** 2026-05-27 con SIMULATION_MODE=1 → 608 KB, 80% partición libre.
**Bring-up real con SIMULATION_MODE=0:** 2026-05-27 → 685 KB, 78% libre, display + touch OK.

## Hallazgos del bring-up real (2026-05-27)

Tres cosas que NO eran obvias del schematic y se descubrieron probando:

1. **`CONFIG_LV_COLOR_16_SWAP=y` es obligatorio.** Sin esto, la imagen se ve
   con cada pixel "pixelado" — los bytes RGB565 quedan en orden inverso entre
   LVGL (little-endian) y el ST7796 (big-endian sobre SPI). Está aplicado en
   `sdkconfig.defaults`.
2. **Mirror correcto: `esp_lcd_panel_mirror(panel, false, false)`.** El
   primer guess `mirror(true, false)` dejaba la imagen espejada horizontalmente.
3. **El touch FT6336 de esta unidad está montado rotado 180° respecto al
   display.** El mapeo que funciona es:
   ```c
   x_land = raw_y;
   y_land = LCD_NATIVE_H_RES - 1 - raw_x;   // 319 - raw_x
   ```
   Si en una unidad futura el touch se ve "espejado" en un solo eje, las
   alternativas son las otras 3 combinaciones:
   - `x=raw_y, y=raw_x` (sin inversión)
   - `x=479-raw_y, y=raw_x` (espejo solo X)
   - `x=479-raw_y, y=319-raw_x` (espejo solo Y)

---

## Pre-vuelo (lee antes de enchufar)

> ⚠️ **Antes de conectar el board a la PCB FA-10T**, revisar
> [CONFLICTOS_PINES.md](CONFLICTOS_PINES.md). Para esta primera prueba
> conviene **probar el display solo**, sin la PCB FA-10T enchufada al
> conector H1/J8. Así descartamos problemas eléctricos y validamos la
> imagen.

Verificá que tenés:

- [ ] Board Waveshare ESP32-S3-Touch-LCD-3.5 (no la versión "-B")
- [ ] Cable USB-C bueno (data, no solo carga)
- [ ] Socket microSD **vacío**
- [ ] Header J2 cámara **sin nada conectado**
- [ ] Conector J8 (header de pines) **sin la PCB FA-10T conectada** (esta prueba)

---

## Paso 1 — Cambiar a modo real

Editar [main/app_config.h](main/app_config.h) línea 10:

```c
#ifndef SIMULATION_MODE
#define SIMULATION_MODE              0      // ← cambiar 1 por 0
#endif
```

---

## Paso 2 — Identificar el puerto COM

1. Enchufar el board por USB-C.
2. En PowerShell:
   ```powershell
   Get-PnpDevice -Class Ports -PresentOnly | Format-Table FriendlyName, InstanceId
   ```
   Buscar algo como **"USB Serial Device (COM*X*)"** o **"Silicon Labs CP210x"**.
3. Anotar el COM (p.ej. `COM5`).

> Si no aparece nada: instalar el driver CP210x desde Silicon Labs, o probar
> con el modo "boot": mantener BOOT, pulsar RESET, soltar BOOT.

---

## Paso 3 — Compilar y flashear

Desde `C:\Users\Emilio\FA-10T-firmware`, con el entorno IDF activado
(`.\activate-idf.ps1`):

```powershell
idf.py build
idf.py -p COM5 flash monitor      # reemplazar COM5
```

El monitor abre en serie a 115200 baud. Para salir: `Ctrl+]`.

---

## Paso 4 — Qué debería pasar (cronología)

| Tiempo  | Log esperado                                                   | Pantalla            |
|---------|----------------------------------------------------------------|---------------------|
| 0 s     | `I (xxx) main: FA-10T v3.0 firmware boot`                      | Negro               |
| ~0.5 s  | `I (xxx) sht31: SHT31 init: SDA=8 SCL=7 addr=0x44`             | Negro               |
| ~0.5 s  | `I (xxx) ssr3ch: ...`, `pt1000_adc: ...`, `acs712: ...`        | Negro               |
| ~1 s    | `I (xxx) ui_task: UI task started`                             | Negro               |
| ~1 s    | `I (xxx) display: ST7796 ready (SPI@40MHz, 480×320 ...)`       | Negro→blanco/ruido  |
| ~1.5 s  | (LVGL dibuja primer frame, `display_set_backlight(true)`)      | **Splash visible**  |
| ~2.5 s  | Pantalla INICIO con setpoint, temperatura actual, botones      | **INICIO**          |
| cada 2s | `I (xxx) ui_task: T=... SP=... drv=...% fan=...%`              | INICIO actualizado  |

### Validación visual

- **Splash:** logo / nombre BIO ORIGEN centrado, fondo oscuro.
- **INICIO:** debe verse `SETPOINT`, `T=xx.x°C`, indicadores de estado de
  los 3 SSR (DRV, FAN, AUX), y los botones de navegación (PROG MANUAL,
  PROG PROGRAMAS, AREA TECNICA).
- **Touch:** tocar un botón debería resaltarlo (LVGL highlight state).
  Para una prueba rápida: tocar **AREA TECNICA**, debería abrir la pantalla
  técnica con campos editables.

---

## Paso 5 — Verificar el touch

1. Con la pantalla INICIO visible, tocar uno de los botones grandes.
2. Esperado: cambia de pantalla suavemente.
3. Si toca y no responde: ver troubleshooting "Touch no responde".

### Diagnóstico manual del touch FT6336

En el log serial buscar `display: FT6336 init failed`. Si aparece, el
expansor TCA9554 está mal o el RST está siempre en bajo (panel y touch
en reset permanente).

Para verificar la presencia de los devices en el bus I2C, agregar
temporalmente al final de `display_init()` (en !SIMULATION_MODE):

```c
ESP_LOGI(TAG, "I2C scan:");
for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (i2c_master_probe(bus, a, 50) == ESP_OK) {
        ESP_LOGI(TAG, "  0x%02X present", a);
    }
}
```

Esperado: 0x20 (TCA9554), 0x34 (AXP2101), 0x38 (FT6336), 0x44 (SHT31 si
está cableado), 0x51 (PCF85063), 0x18 (ES8311), 0x6A (QMI8658).

---

## Troubleshooting

### Pantalla queda en negro completo, sin backlight

- **Causa probable:** `init_lcd_hardware()` falló y el log lo reporta.
- Mirar el log: `LCD hw init failed: <error>`.
- Si el error es `ESP_ERR_NOT_FOUND` en `TCA9554`: el expansor no responde
  → check direcciones I2C (debe ser 0x20).
- Si el error es en `esp_lcd_panel_init`: el reset no llegó al ST7796.

### Pantalla blanca o ruido permanente

- Backlight encendido pero panel no inicializa → el SPI no llega o el
  reset está mal.
- Probar bajar `LCD_PIXEL_CLOCK_HZ` de 40 MHz a 20 MHz en
  [main/display_pins.h](main/display_pins.h):
  ```c
  #define LCD_PIXEL_CLOCK_HZ       (20 * 1000 * 1000)
  ```

### Pantalla con colores invertidos (rojo↔azul)

- Cambiar el endian: en `display.c::init_lcd_hardware()`:
  ```c
  .rgb_endian = LCD_RGB_ENDIAN_RGB,    // era BGR
  ```

### Pantalla con colores demasiado oscuros / "negativo"

- Quitar la inversión: comentar la línea
  ```c
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
  ```
  El panel JHD0350 a veces se entrega con `LCD_INVON` ya activo por
  defecto, otras no.

### Pantalla con la imagen rotada/espejada

- En `init_lcd_hardware()` ajustar el par `swap_xy` + `mirror`:
  | Lo que ves          | Cambiar a                              |
  |---------------------|----------------------------------------|
  | Rotada 180°         | `mirror(true, true)`                   |
  | Espejada horizontal | `mirror(false, false)`                 |
  | Rotada 90° derecha  | `swap_xy(true)` + `mirror(true, false)` (actual) |
  | Rotada 90° izquierda| `swap_xy(true)` + `mirror(false, true)`|

### Touch responde pero las coordenadas están invertidas / desalineadas

- **Forma rápida de diagnosticar**: agregar un dot rojo en la top layer de
  LVGL que siga el touch en cada `indev_read_cb` (ver historia git del
  2026-05-27 para el snippet completo). Con el dot visible, una sola pasada
  del dedo de izquierda a derecha y de arriba a abajo te dice cuál eje está
  invertido.
- Editar `tp_read_xy()` en `display.c`: cambiar el mapeo (4 combinaciones
  posibles según la orientación física del touch en esta unidad):
  | Si tocás aquí | LVGL recibe | Mapeo correcto                        |
  |---------------|-------------|---------------------------------------|
  | arriba-izq    | arriba-izq  | `*x=raw_y; *y=raw_x` (sin inversión, rotación 90° CW) |
  | arriba-izq    | abajo-der   | `*x=raw_y; *y=319-raw_x` ← **mapeo actual** (rotación 90° + 180°) |
  | arriba-izq    | abajo-izq   | `*x=479-raw_y; *y=raw_x` (rotación 90° CCW) |
  | arriba-izq    | arriba-der  | `*x=479-raw_y; *y=319-raw_x` (CCW + 180°) |

### Touch no responde para nada

1. Confirmar en serial que apareció `display: ST7796 ready` (sin error).
2. Confirmar que NO apareció `display: FT6336 init failed`.
3. Hacer scan I2C (ver Paso 5) — debe aparecer `0x38 present`.
4. Si 0x38 no aparece: el TP_RST sigue en reset. Verificar que
   `ioexp_set_lcd_tp_reset(false)` se llamó (revisar logs).

### Display funciona pero el firmware se reinicia (panic) cuando dibuja

- Probable causa: heap insuficiente. Los buffers DMA piden 2 × 480 × 40 ×
  2 B = ~76 KB de RAM interna DMA-capable. Si el RTOS u otros drivers
  ya consumieron mucho, fallarán.
- Reducir `LVGL_BUF_LINES` de 40 a 20:
  ```c
  #define LVGL_BUF_LINES       20    // 480 × 20 × 2B = ~19 KB cada buffer
  ```

### Compila pero el log dice `shared I2C bus not initialised`

- `sht31_init()` falló (probablemente porque el SHT31 no está cableado en
  esta prueba aislada). El driver crea el bus aunque el dispositivo no
  responda, así que normalmente sigue. Si NO crea el bus: forzar la
  inicialización del bus I2C directamente en `display_init()` con
  `i2c_new_master_bus(...)` en lugar de depender de SHT31.

---

## Cuando todo funcione

1. Volver a editar [main/app_config.h](main/app_config.h):
   - dejar `SIMULATION_MODE = 0` para producción.
2. **Antes de enchufar la PCB FA-10T al conector J8 del Waveshare,
   resolver los conflictos de pines listados en**
   [CONFLICTOS_PINES.md](CONFLICTOS_PINES.md).
3. Recién después, encender el conjunto completo (display + PCB) y
   verificar que las temperaturas/corrientes son verosímiles.

---

## Anexo — Backups útiles

- Schematic Waveshare local: `C:\Users\Emilio\AppData\Local\Temp\ws_schematic.pdf`
- Pin source-of-truth: [main/display_pins.h](main/display_pins.h)
- Conflictos detallados: [CONFLICTOS_PINES.md](CONFLICTOS_PINES.md)
