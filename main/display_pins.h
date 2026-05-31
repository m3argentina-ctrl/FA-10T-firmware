// Control ESP32-S3 v3.0 — Waveshare ESP32-S3-Touch-LCD-3.5 pinout
//
// FUENTE PRIMARIA: schematic oficial Waveshare
//   https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Schematic.pdf
//   (descargado 2026-05-27, MD5 verificado contra netlist).
//
// FUENTE SECUNDARIA: wiki Waveshare
//   https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5
//
// Variante: "sin B" (versión estándar, panel JHD0350A007V1-A1I2900,
//           controlador ST7796, touch FT6336, RTC PCF85063, PMU AXP2101,
//           expansor I/O TCA9554PWR, 8MB PSRAM OCT, 16MB flash QIO).

#pragma once

#include "driver/spi_master.h"

// -----------------------------------------------------------------------------
// LCD ST7796 — SPI bus
// -----------------------------------------------------------------------------
// Net schematic   | ESP32-S3 GPIO | Notas
// -----------------+---------------+-----------------------------------
// LCD_SPI_MOSI    | GPIO1         | también se usa como LCD_QSPI_IO0 si el firmware operase en QSPI
// LCD_SPI_MISO    | GPIO2         | también LCD_QSPI_IO1
// LCD_DC          | GPIO3         | también LCD_QSPI_IO2
// LCD_SPI_SCLK    | GPIO5         | también LCD_QSPI_SCLK
// LCD_BL          | GPIO6         | base de S8050; HIGH = backlight ON
// LCD_CS          | (sin GPIO)    | atado a GND vía R16 = 10 kΩ — chip select permanente
// LCD_RST         | EXIO1 (TCA9554) | compartido con TP_RST
// -----------------------------------------------------------------------------
#define LCD_HOST                 SPI2_HOST
#define LCD_PIN_MOSI             1     // schematic NLLCD0SPI0MOSI / PIU106 — GPIO1
#define LCD_PIN_MISO             2     // schematic NLLCD0SPI0MISO / PIU107 — GPIO2 (no se usa en escritura)
#define LCD_PIN_DC               3     // schematic NLLCD0DC        / PIU108 — GPIO3
#define LCD_PIN_SCLK             5     // schematic NLLCD0SPI0SCLK  / PIU1010 — GPIO5
#define LCD_PIN_BL               6     // schematic NLLCD0BL        / PIU1011 — GPIO6 (activo alto)
#define LCD_PIN_CS              -1     // tied a GND vía R16=10kΩ — pasar -1 a esp_lcd_panel_io_spi_config_t

// Pixel clock: el demo oficial de Waveshare especifica 80 MHz, pero en esta
// unidad la integridad de señal del trazado SoC → FPC del LCD se degrada y
// los pixels de textos chicos (cajitas con captions xs/md) se ven
// "pixelados" — los bits de RGB565 llegan corruptos. Volvemos a 40 MHz que
// fue el valor validado en el bring-up original y que da una imagen limpia.
// Si quisieras probar un punto intermedio, 60 MHz es la siguiente opción a
// validar (puede tener menos jitter aún manteniendo mejor frame rate).
#define LCD_PIXEL_CLOCK_HZ       (40 * 1000 * 1000)
#define LCD_CMD_BITS             8
#define LCD_PARAM_BITS           8

// Resolución del panel: nativo 320×480 (portrait). Usamos landscape → 480×320.
#define LCD_NATIVE_H_RES         320
#define LCD_NATIVE_V_RES         480

// -----------------------------------------------------------------------------
// Touch FT6336 — I2C
// -----------------------------------------------------------------------------
// Net schematic   | ESP32-S3 GPIO | Notas
// -----------------+---------------+-----------------------------------
// TP_SDA (=TWI_SDA) | GPIO8       | bus I2C COMPARTIDO con SHT31, AXP2101, PCF85063, ES8311, QMI8658, TCA9554
// TP_SCL (=TWI_CLK) | GPIO7       |
// TP_INT          | EXIO2 (TCA9554) | input (no se usa — el touch se lee por polling cada frame LVGL)
// TP_RST          | EXIO1 (TCA9554) | compartido con LCD_RST (un solo reset para ambos)
// -----------------------------------------------------------------------------
#define TP_I2C_ADDR              0x38   // FT6336 dirección fija 7-bit
#define TP_I2C_SPEED_HZ          400000

// -----------------------------------------------------------------------------
// Expansor I/O TCA9554PWR (controla las señales que no entran en GPIOs)
// -----------------------------------------------------------------------------
// Dirección I2C: A0=A1=A2=GND (R34 NC, R35=R36 pull-down 10k) → 0x20
// Bits (puerto P0..P7):
//   EXIO0  P0  CAM_PWDN    output
//   EXIO1  P1  LCD_RST + TP_RST   output (reset compartido)
//   EXIO2  P2  TP_INT      input
//   EXIO3  P3  SD_CS       output (no usamos SD)
//   EXIO4  P4  RTC_INT     input
//   EXIO5  P5  AXP_IRQ     input
//   EXIO6  P6  SYS_OUT     output (control de carga 3.3V de periféricos)
//   EXIO7  P7  PA_CTRL     output (amplificador de audio NS4150)
// -----------------------------------------------------------------------------
#define IOEXP_I2C_ADDR           0x20
#define IOEXP_I2C_SPEED_HZ       400000

#define IOEXP_BIT_CAM_PWDN       (1u << 0)
#define IOEXP_BIT_LCD_TP_RST     (1u << 1)
#define IOEXP_BIT_TP_INT         (1u << 2)
#define IOEXP_BIT_SD_CS          (1u << 3)
#define IOEXP_BIT_RTC_INT        (1u << 4)
#define IOEXP_BIT_AXP_IRQ        (1u << 5)
#define IOEXP_BIT_SYS_OUT        (1u << 6)
#define IOEXP_BIT_PA_CTRL        (1u << 7)

// Registros del TCA9554 (datasheet TI SCPS207H):
#define IOEXP_REG_INPUT          0x00
#define IOEXP_REG_OUTPUT         0x01
#define IOEXP_REG_POLARITY       0x02
#define IOEXP_REG_CONFIG         0x03   // 1 = input, 0 = output
// Config inicial: outputs = CAM_PWDN, LCD_TP_RST, SD_CS, SYS_OUT, PA_CTRL
// inputs = TP_INT, RTC_INT, AXP_IRQ
#define IOEXP_CONFIG_DEFAULT     (IOEXP_BIT_TP_INT | IOEXP_BIT_RTC_INT | IOEXP_BIT_AXP_IRQ)
// Estado inicial de outputs: LCD/TP en reset (0), todo lo demás en estado seguro.
// SYS_OUT=1 enciende periféricos no críticos (no necesario para LCD).
// PA_CTRL=0 deja audio mute.
#define IOEXP_OUTPUT_RESET       0x00                       // LCD/TP RST asserted (low)
#define IOEXP_OUTPUT_RUN         (IOEXP_BIT_LCD_TP_RST)     // LCD/TP RST released

// -----------------------------------------------------------------------------
// Conflictos con la PCB FA-10T  (ver CONFLICTOS_PINES.md para el análisis)
// -----------------------------------------------------------------------------
// El Waveshare expone IO9, IO10, IO11 al socket microSD CON pull-ups 10 kΩ a
// 3V3 (R20, R22, R21). En la PCB FA-10T esos mismos pines llevan:
//   GPIO9  = I_FAN
//   GPIO10 = ACS712 V_OUT (ADC1_CH9)
//   GPIO11 = PT1000 V_ADC (ADC2_CH0)
// La presencia simultánea de los pull-ups del board y los divisores resistivos
// de la PCB hace que ambos lados se interfieran. NO es resoluble por software.
// -----------------------------------------------------------------------------
