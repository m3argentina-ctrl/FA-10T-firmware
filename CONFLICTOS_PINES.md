# CONFLICTOS DE PINES — Control ESP32-S3 v3.0 + Waveshare ESP32-S3-Touch-LCD-3.5

**Fecha:** 2026-05-27
**Fuente schematic Waveshare:** [ESP32-S3-Touch-LCD-3.5-Schematic.pdf](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Schematic.pdf)

## Resumen ejecutivo

> **Hay 6 conflictos** entre los GPIOs que la PCB FA-10T espera usar y los GPIOs
> que el board Waveshare ya tiene cableados a periféricos onboard (microSD y
> conector cámara). De esos 6, **2 son críticos para señales ADC analógicas**
> (PT1000 y ACS712) y NO pueden resolverse por software.

## Mapa completo de pines

| GPIO  | PCB FA-10T espera           | Waveshare onboard                  | ¿Conflicto?               |
|-------|-----------------------------|------------------------------------|---------------------------|
| GPIO0 | BTN_RESET (BOOT)            | EN/BOOT button                     | ✅ Coincide (es el mismo) |
| GPIO1 | —                           | LCD_MOSI                           | OK (libre en PCB)         |
| GPIO2 | —                           | LCD_MISO                           | OK                        |
| GPIO3 | —                           | LCD_DC                             | OK                        |
| GPIO4 | —                           | (libre)                            | OK                        |
| GPIO5 | —                           | LCD_SCLK                           | OK                        |
| GPIO6 | —                           | LCD_BL (backlight)                 | OK                        |
| **GPIO7** | **I2C_SCL (SHT31)**       | **I2C SCL compartido (bus único)** | **✅ NO es conflicto: mismo bus** |
| **GPIO8** | **I2C_SDA (SHT31)**       | **I2C SDA compartido (bus único)** | **✅ NO es conflicto: mismo bus** |
| **GPIO9** | **I_FAN (entrada digital)** | SD_MISO + pull-up 10kΩ a 3V3 (R20) + pin del conector J8 | ⚠️ **CONFLICTO** |
| **GPIO10**| **ACS712 V_OUT (ADC1_CH9)** | SD_MOSI + pull-up 10kΩ a 3V3 (R22) + pin J8 | ⚠️⚠️ **CONFLICTO CRÍTICO (ADC)** |
| **GPIO11**| **PT1000 V_ADC (ADC2_CH0)** | SD_SCLK + pull-up 10kΩ a 3V3 (R21) + pin J8 | ⚠️⚠️ **CONFLICTO CRÍTICO (ADC)** |
| GPIO12–16 | —                       | I2S (codec ES8311)                 | OK (libre en PCB)         |
| **GPIO17**| **SSR_FAN**               | CAM_VSYNC (header cámara)          | ⚠️ **CONFLICTO** (manejable) |
| **GPIO18**| **SSR_AUX**               | CAM_HREF                           | ⚠️ **CONFLICTO** (manejable) |
| GPIO19–20 | —                       | USB D-/D+                          | OK                        |
| **GPIO21**| **SSR_DRV**               | CAM_D7                             | ⚠️ **CONFLICTO** (manejable) |
| GPIO33–48 | —                       | flash externa + cámara             | OK                        |

## Detalle por conflicto

### 1. GPIO9 — I_FAN vs SD_MISO  (⚠️ manejable)

- **Waveshare:** R20 = 10kΩ pull-up a 3V3, ruta al socket microSD (J4 pin 7).
- **FA-10T:** entrada digital del sensor de presencia de turbina.
- **Impacto:** el pull-up forzaría I_FAN a HIGH permanente cuando no haya
  tarjeta SD insertada. Lectura inválida.
- **Mitigación:** desoldar R20 del Waveshare *o* dejar el conector SD vacío y
  no usar I_FAN (se está usando ACS712 RMS en GPIO10 para la misma función;
  I_FAN podría quedar sin conectar en esta versión del firmware).

### 2. GPIO10 — ACS712 V_OUT (ADC) vs SD_MOSI  (⚠️⚠️ CRÍTICO)

- **Waveshare:** R22 = 10kΩ pull-up a 3V3, ruta al socket microSD (J4 pin 3).
- **FA-10T:** entrada analógica ADC1_CH9, esperaba ver 2.5 V ± (0.185 V × I_A).
- **Impacto:** el pull-up de 10kΩ a 3V3 forma un divisor con la impedancia
  de salida del ACS712 (≈ 1Ω) y con cualquier filtro RC en la PCB. La
  lectura va a estar dominada por 3V3, no por el ACS712. **NO funciona.**
- **Mitigación:**
  - **Opción A (recomendada):** desoldar R22 del board Waveshare.
  - **Opción B:** rutear la salida del ACS712 por cable a otro GPIO libre
    (GPIO4 está disponible y es ADC1_CH3).
  - **Opción C (PCB v2):** mover ACS712 a GPIO4 en la próxima revisión.

### 3. GPIO11 — PT1000 V_ADC (ADC) vs SD_SCLK  (⚠️⚠️ CRÍTICO)

- **Waveshare:** R21 = 10kΩ pull-up a 3V3, ruta al socket microSD (J4 pin 5).
- **FA-10T:** divisor 3V3 → 2.2kΩ → V_adc → 100Ω → PT1000 → GND.
- **Impacto:** los 10kΩ adicionales en paralelo con el ramal alto del divisor
  alteran completamente la relación R(T) → V(adc). El cálculo Callendar–Van
  Dusen daría temperaturas falsas y errores de "open" / "short".
- **Mitigación:**
  - **Opción A (recomendada):** desoldar R21.
  - **Opción B:** rutear PT1000 por cable a otro pin ADC2 libre.
    GPIO15 y GPIO16 están ocupados por I2S; GPIO12, 13, 14 idem.
    **No queda ADC2 libre fácilmente**; lo más limpio es usar un ADC1
    como GPIO4 (CH3) y reservar GPIO10 para ACS712 también vía cable.
  - **Opción C (PCB v2):** rediseñar para evitar GPIOs 9–11 enteros.

### 4–6. GPIO17, GPIO18, GPIO21 — SSRs vs cámara  (⚠️ manejable)

- **Waveshare:** señales de la interfaz DVP de cámara (J2). El header está
  vacío de fábrica (no hay módulo OV2640 conectado) y **no hay pull-ups
  externos** en estos pines según el schematic.
- **FA-10T:** drivers de SSR por opto PC817, controlados time-proporcional.
- **Impacto:** sin módulo de cámara, los pines están eléctricamente libres
  (alta impedancia hacia el header J2). Funcionan como GPIO output normal.
- **Mitigación:** **ninguna necesaria.** Solo asegurarse de NUNCA enchufar un
  módulo de cámara al header J2.

## Tabla de prioridad de acción

| Acción                                                            | Urgencia | Quien       |
|-------------------------------------------------------------------|----------|-------------|
| Desoldar R21 (pull-up SD_SCLK / GPIO11 / PT1000)                  | 🔴 ALTA  | Hardware    |
| Desoldar R22 (pull-up SD_MOSI / GPIO10 / ACS712)                  | 🔴 ALTA  | Hardware    |
| Desoldar R20 (pull-up SD_MISO / GPIO9 / I_FAN)                    | 🟡 MEDIA | Hardware    |
| Dejar el socket microSD vacío (no insertar tarjeta)               | 🔴 ALTA  | Operativo   |
| Dejar el header de cámara J2 vacío                                | 🔴 ALTA  | Operativo   |
| Reservar GPIO4 como pin ADC alternativo para PCB v2               | 🟢 BAJA  | Diseño PCB  |

## Validación recomendada antes de flashear con SIMULATION_MODE=0

1. Verificar visualmente que las resistencias R20, R21, R22 están **removidas**.
2. Confirmar socket microSD **sin tarjeta**.
3. Confirmar header J2 cámara **sin módulo conectado**.
4. Si NO se pueden remover las resistencias: probar primero solo el display
   sin conectar la PCB FA-10T, y diferir las lecturas ADC para una v2 del
   PCB que use otros GPIOs.

## Cosas que NO son conflicto (verificadas)

- **I2C compartido GPIO7/GPIO8:** correcto. El driver SHT31 ya crea el bus
  con `i2c_new_master_bus()` y expone `sht31_shared_bus()` para que el TCA9554,
  el touch FT6336 y el RTC PCF85063 se agreguen como dispositivos.
- **Direcciones I2C:** SHT31=0x44, FT6336=0x38, TCA9554=0x20, AXP2101=0x34,
  PCF85063=0x51, ES8311=0x18, QMI8658=0x6A. Sin colisiones.
- **LCD_CS sin GPIO:** R16 lo deja atado a GND. `esp_lcd_panel_io_spi_config_t`
  con `cs_gpio_num = -1` y el panel ST7796 queda permanentemente seleccionado.
  Funciona porque es el único dispositivo en SPI2_HOST.
