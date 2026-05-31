// Control SPE32-S3 v3.0 — Display + Touch + LVGL bridge for Waveshare ESP32-S3-Touch-LCD-3.5.
//
// Pinout: ver main/display_pins.h (extraído del schematic oficial Waveshare).
// Conflictos hardware con la PCB FA-10T: ver CONFLICTOS_PINES.md (raíz del repo).
//
// Arquitectura:
//   - LCD ST7796 por SPI2_HOST (CS atado a GND, RST por expansor TCA9554).
//   - Touch FT6336 por el bus I2C compartido (mismo que SHT31, RTC, etc.).
//   - TCA9554PWR (I/O expander en 0x20) controla LCD_RST + TP_RST (compartidos
//     en EXIO1) y lee TP_INT (EXIO2; aquí ignorado, se usa polling).
//   - LVGL 8.4 con doble buffer parcial en RAM interna DMA-capable.

#include "display.h"
#include "display_pins.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "audio.h"   // audio_click() on every touch PRESS transition

#if !SIMULATION_MODE
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"      // PWM del backlight (control de brillo)
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"   // managed_component espressif/esp_lcd_st7796
#include "sht31.h"            // i2c_master_bus_handle_t sht31_shared_bus(void)
#include "nvs.h"              // brillo persistido en namespace propio "fa10t_disp"
#endif

static const char *TAG = "display";

#define LVGL_BUF_LINES       40    // partial buffer height (480 × 40 × 2B ≈ 38 KB)

// Backlight PWM (LEDC). GPIO6 maneja la base del transistor S8050 (activo alto);
// una PWM de 5 kHz permite atenuar el panel de DISPLAY_BL_PCT_MIN..100 %. Low-
// speed mode, 10 bits de resolución (duty 0..1023). 80 MHz/1024 ≈ 78 kHz de tope
// para 10 bits, así que 5 kHz entra holgado.
#define BL_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER        LEDC_TIMER_0
#define BL_LEDC_CHANNEL      LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ      5000
#define BL_LEDC_RES          LEDC_TIMER_10_BIT
#define BL_LEDC_DUTY_MAX     ((1u << 10) - 1u)   // 1023

// Brillo de reposo cuando ui_task detecta inactividad (auto-atenuado). Fijo en
// firmware. Mantenerlo legible (>= mínimo) para que el panel no quede negro.
#define DISPLAY_BL_DIM_PCT   15

static SemaphoreHandle_t      s_lvgl_mtx;
static esp_timer_handle_t     s_lvgl_tick_timer;
static lv_disp_draw_buf_t     s_draw_buf;
static lv_disp_drv_t          s_disp_drv;
static lv_indev_drv_t         s_indev_drv;
static lv_color_t            *s_buf1;
static lv_color_t            *s_buf2;
static uint8_t                s_bl_pct = 100;   // brillo backlight (cargado de NVS al init)
static bool                   s_bl_dimmed = false; // true = atenuado por inactividad

#if !SIMULATION_MODE
static esp_lcd_panel_handle_t      s_panel;
static esp_lcd_panel_io_handle_t   s_panel_io;
static i2c_master_dev_handle_t     s_tp_dev;
static i2c_master_dev_handle_t     s_ioexp_dev;
static uint8_t                     s_ioexp_out_shadow;
#endif

// -----------------------------------------------------------------------------
// LVGL locking primitives (used by ui_task and any external lv_obj manipulation)
// -----------------------------------------------------------------------------
bool display_lvgl_lock(int timeout_ms)
{
    if (!s_lvgl_mtx) return false;
    const TickType_t t = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mtx, t) == pdTRUE;
}

void display_lvgl_unlock(void)
{
    if (s_lvgl_mtx) xSemaphoreGiveRecursive(s_lvgl_mtx);
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

// -----------------------------------------------------------------------------
// LCD flush callback (LVGL → ST7796)
// -----------------------------------------------------------------------------
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
#if SIMULATION_MODE
    (void)area; (void)color_map;
    lv_disp_flush_ready(drv);
#else
    if (s_panel) {
        esp_lcd_panel_draw_bitmap(s_panel,
                                  area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1,
                                  color_map);
        // notify_flush_ready() (called from esp_lcd ISR) will release LVGL.
    } else {
        lv_disp_flush_ready(drv);
    }
#endif
}

#if !SIMULATION_MODE
static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata,
                               void *user_ctx)
{
    (void)panel_io; (void)edata;
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

// -----------------------------------------------------------------------------
// TCA9554 I/O expander helpers
// -----------------------------------------------------------------------------
static esp_err_t ioexp_write_reg(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_ioexp_dev, buf, sizeof(buf), 50);
}

static esp_err_t ioexp_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = IOEXP_I2C_ADDR,
        .scl_speed_hz    = IOEXP_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_ioexp_dev),
                        TAG, "ioexp add_dev");

    // 1) put RST low (asserted) before configuring direction.
    s_ioexp_out_shadow = IOEXP_OUTPUT_RESET;
    ESP_RETURN_ON_ERROR(ioexp_write_reg(IOEXP_REG_OUTPUT, s_ioexp_out_shadow),
                        TAG, "ioexp out");
    // 2) configure direction (1 = input, 0 = output).
    ESP_RETURN_ON_ERROR(ioexp_write_reg(IOEXP_REG_CONFIG, IOEXP_CONFIG_DEFAULT),
                        TAG, "ioexp cfg");
    return ESP_OK;
}

static void ioexp_set_lcd_tp_reset(bool asserted_low)
{
    if (!s_ioexp_dev) return;
    if (asserted_low) {
        s_ioexp_out_shadow &= (uint8_t)~IOEXP_BIT_LCD_TP_RST;
    } else {
        s_ioexp_out_shadow |= IOEXP_BIT_LCD_TP_RST;
    }
    (void)ioexp_write_reg(IOEXP_REG_OUTPUT, s_ioexp_out_shadow);
}

// -----------------------------------------------------------------------------
// FT6336 touch read
// -----------------------------------------------------------------------------
static esp_err_t tp_read_xy(uint16_t *x, uint16_t *y, bool *pressed)
{
    // FT6336 register map:
    //   0x02 = TD_STATUS (low nibble = number of touch points)
    //   0x03 = P1_XH (high 4 bits of X, event flag in bits 7..6)
    //   0x04 = P1_XL
    //   0x05 = P1_YH
    //   0x06 = P1_YL
    uint8_t reg = 0x02;
    uint8_t buf[5] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_tp_dev, &reg, 1, buf, sizeof(buf), 50);
    if (err != ESP_OK) return err;

    uint8_t n = buf[0] & 0x0F;
    if (n == 0 || n > 2) {
        *pressed = false;
        return ESP_OK;
    }
    uint16_t raw_x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
    uint16_t raw_y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];

    // Panel nativo es portrait 320×480; trabajamos en landscape 480×320 con
    // swap_xy(true)+mirror(false,false) en el display. El controlador touch
    // FT6336 de esta unidad está montado físicamente rotado 180° respecto al
    // display, así que el mapeo es: x_land = raw_y, y_land = (319 - raw_x).
    // (Verificado el 2026-05-27 con el dot rojo de debug en pantalla.)
    if (raw_y > LCD_NATIVE_V_RES - 1) raw_y = LCD_NATIVE_V_RES - 1;
    if (raw_x > LCD_NATIVE_H_RES - 1) raw_x = LCD_NATIVE_H_RES - 1;
    *x = raw_y;
    *y = (uint16_t)(LCD_NATIVE_H_RES - 1 - raw_x);
    *pressed = true;
    return ESP_OK;
}

static esp_err_t tp_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TP_I2C_ADDR,
        .scl_speed_hz    = TP_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &dev_cfg, &s_tp_dev);
}
#endif // !SIMULATION_MODE

// -----------------------------------------------------------------------------
// LVGL input device read callback
// -----------------------------------------------------------------------------
static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
#if SIMULATION_MODE
    data->state = LV_INDEV_STATE_RELEASED;
#else
    static int16_t last_x = 0, last_y = 0;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    static bool was_pressed = false;
    // Press latch: cuando detectamos un PRESS real, lo "estiramos" durante
    // PRESS_LATCH_FRAMES polls más, aún si el FT6336 ya reporta released.
    // Esto resuelve el bug clásico de tactos breves perdidos: si un toque
    // dura menos que el período de polling (30 ms default), LVGL puede no
    // ver el PRESSED → no dispara click. Con el latch garantizamos que
    // siempre hay una ventana PRESSED de al menos 3 ciclos para que LVGL
    // detecte la transición PRESSED→RELEASED.
    static uint8_t press_latch = 0;
    // 5 frames × 15 ms = 75 ms de latch. Cubre el caso típico de FUNC_PROGRAMAS
    // donde el update_cb de 500 ms puede causar un frame de render largo y la
    // ventana entre dos polls reales se estira. Antes era 3 (45 ms) y todavía
    // se perdían algunos tactos durante la ejecución del programa.
    enum { PRESS_LATCH_FRAMES = 5 };
    if (s_tp_dev && tp_read_xy(&x, &y, &pressed) == ESP_OK && pressed) {
        if (x > LCD_H_RES - 1) x = LCD_H_RES - 1;
        if (y > LCD_V_RES - 1) y = LCD_V_RES - 1;
        last_x = (int16_t)x;
        last_y = (int16_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
        press_latch = PRESS_LATCH_FRAMES;
        // Click acústico solo en la transición RELEASED→PRESSED para que un
        // toque sostenido no genere una ráfaga continua de beeps.
        if (!was_pressed) {
            audio_click();
            was_pressed = true;
        }
    } else if (press_latch > 0) {
        // El FT6336 ya no reporta presión, pero estiramos el PRESS unos
        // ciclos más para que LVGL alcance a ver la transición y dispare
        // el click event aunque el toque haya sido más corto que el período
        // de polling.
        press_latch--;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        was_pressed = false;
    }
    data->point.x = last_x;
    data->point.y = last_y;
#endif
}

#if !SIMULATION_MODE
// -----------------------------------------------------------------------------
// Backlight PWM helpers
// -----------------------------------------------------------------------------
// Mapea 0..100 % → duty del LEDC y lo empuja al canal.
static void bl_apply(uint8_t pct)
{
    uint32_t duty = (uint32_t)pct * BL_LEDC_DUTY_MAX / 100u;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

// Lee el brillo guardado de su propio namespace NVS (independiente del blob de
// config versionado, así nunca perturba PID/setpoint). Si no hay valor válido
// deja el default (100 %).
static void bl_load_pct(void)
{
    nvs_handle_t h;
    if (nvs_open("fa10t_disp", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    if (nvs_get_u8(h, "bl_pct", &v) == ESP_OK && v >= DISPLAY_BL_PCT_MIN && v <= 100) {
        s_bl_pct = v;
    }
    nvs_close(h);
}

// -----------------------------------------------------------------------------
// LCD hardware init
// -----------------------------------------------------------------------------
static esp_err_t init_lcd_hardware(void)
{
    // 1) Backlight por PWM (LEDC) en GPIO6 (base del S8050, activo alto).
    //    Arranca en duty 0 (APAGADO) hasta que LVGL haya volcado el primer
    //    frame; después ui_task lo lleva al brillo guardado. La PWM permite
    //    atenuar el panel desde ÁREA TÉCNICA.
    ledc_timer_config_t bl_timer = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "bl ledc timer");
    ledc_channel_config_t bl_ch = {
        .gpio_num   = LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,            // APAGADO hasta el primer frame
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_ch), TAG, "bl ledc ch");
    bl_load_pct();                  // lee el brillo guardado (sigue apagado por ahora)

    // 2) SPI bus.
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = LCD_PIN_MISO,
        .sclk_io_num     = LCD_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_BUF_LINES * (int)sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    // 3) Panel I/O over SPI. CS = -1 because R16 ties it to GND.
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num         = LCD_PIN_DC,
        .cs_gpio_num         = LCD_PIN_CS,
        .pclk_hz             = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits        = LCD_CMD_BITS,
        .lcd_param_bits      = LCD_PARAM_BITS,
        .spi_mode            = 0,
        .trans_queue_depth   = 10,
        .on_color_trans_done = notify_flush_ready,
        .user_ctx            = &s_disp_drv,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                                 &io_cfg, &s_panel_io),
                        TAG, "panel io");

    // 4) ST7796 panel. reset_gpio_num = -1 because LCD_RST is on the I/O
    //    expander; we drive it manually below.
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,   // ESP-IDF v6: era rgb_endian en v5
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_panel_io, &panel_cfg, &s_panel),
                        TAG, "st7796");

    // 5) Hardware reset pulse through the TCA9554. ST7796 datasheet: RST low
    //    for ≥ 10 µs, then wait ≥ 120 ms before sending commands.
    ioexp_set_lcd_tp_reset(true);     // assert reset (low)
    vTaskDelay(pdMS_TO_TICKS(20));
    ioexp_set_lcd_tp_reset(false);    // release
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    // Landscape 480×320 con origen top-left: probado vs. unidad real 2026-05-27.
    // Si la imagen aparece espejada en X, cambiar el primer arg a true.
    // Si aparece espejada en Y (rotada 180°), cambiar el segundo arg a true.
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_LOGI(TAG, "ST7796 ready (SPI@%dMHz, 480×320 landscape, BGR)",
             LCD_PIXEL_CLOCK_HZ / 1000000);
    return ESP_OK;
}
#endif // !SIMULATION_MODE

// -----------------------------------------------------------------------------
// Public: display_init
// -----------------------------------------------------------------------------
esp_err_t display_init(void)
{
    s_lvgl_mtx = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mtx) return ESP_ERR_NO_MEM;

    lv_init();

    const size_t buf_pixels = LCD_H_RES * LVGL_BUF_LINES;
    const uint32_t caps =
#if SIMULATION_MODE
        MALLOC_CAP_8BIT;
#else
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
#endif
    s_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), caps);
    s_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), caps);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "LVGL buffer alloc failed (need 2× %u bytes)",
                 (unsigned)(buf_pixels * sizeof(lv_color_t)));
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);

#if !SIMULATION_MODE
    // Shared I2C bus is owned by sht31.c. main.c calls sht31_init() before
    // display_init(), so the bus must be up by now.
    i2c_master_bus_handle_t bus = sht31_shared_bus();
    if (!bus) {
        ESP_LOGE(TAG, "shared I2C bus not initialised — call sht31_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ioexp_init(bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 init failed: %s — LCD cannot be reset, aborting",
                 esp_err_to_name(err));
        return err;
    }

    err = init_lcd_hardware();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD hw init failed: %s — continuing with stub flush",
                 esp_err_to_name(err));
    }

    // Touch is best-effort: if it fails the UI still renders without input.
    esp_err_t tp_err = tp_init(bus);
    if (tp_err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336 init failed (%s) — touch disabled",
                 esp_err_to_name(tp_err));
    }
#else
    ESP_LOGI(TAG, "display init [SIMULATION_MODE] — LVGL with stub flush");
#endif

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.hor_res  = LCD_H_RES;
    s_disp_drv.ver_res  = LCD_V_RES;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = indev_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    const esp_timer_create_args_t targs = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_lvgl_tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer, 1000), TAG, "tick start");

    return ESP_OK;
}

void display_set_backlight(uint8_t pct)
{
#if !SIMULATION_MODE
    if (pct < DISPLAY_BL_PCT_MIN) pct = DISPLAY_BL_PCT_MIN;
    if (pct > 100)                pct = 100;
    s_bl_pct = pct;
    s_bl_dimmed = false;   // fijar brillo es acción activa → salir del reposo
    bl_apply(pct);
#else
    (void)pct;
#endif
}

void display_backlight_set_dimmed(bool dimmed)
{
#if !SIMULATION_MODE
    if (dimmed == s_bl_dimmed) return;   // idempotente: no reescribir el PWM
    s_bl_dimmed = dimmed;
    bl_apply(dimmed ? DISPLAY_BL_DIM_PCT : s_bl_pct);
#else
    s_bl_dimmed = dimmed;
#endif
}

uint8_t display_get_backlight(void)
{
    return s_bl_pct;
}

void display_backlight_save(void)
{
#if !SIMULATION_MODE
    nvs_handle_t h;
    if (nvs_open("fa10t_disp", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, "bl_pct", s_bl_pct) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "brillo guardado: %u%%", (unsigned)s_bl_pct);
#endif
}

void display_set_pa_amp(bool on)
{
#if !SIMULATION_MODE
    if (!s_ioexp_dev) return;
    if (on) s_ioexp_out_shadow |=  IOEXP_BIT_PA_CTRL;
    else    s_ioexp_out_shadow &= ~IOEXP_BIT_PA_CTRL;
    (void)ioexp_write_reg(IOEXP_REG_OUTPUT, s_ioexp_out_shadow);
#else
    (void)on;
#endif
}
