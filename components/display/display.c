#include "display.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "lvgl.h"

static const char *TAG = "display";

#define LVGL_TICK_PERIOD_MS    2
#define LVGL_BUFFER_LINES     40

static SemaphoreHandle_t    s_lvgl_mtx;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static lv_disp_drv_t        s_disp_drv;
static lv_disp_draw_buf_t   s_draw_buf;
static lv_color_t          *s_buf1;
static lv_color_t          *s_buf2;
static display_config_t     s_cfg;
static esp_timer_handle_t   s_lvgl_tick_timer;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
}

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

esp_err_t display_lvgl_lock(int timeout_ms)
{
    TickType_t t = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive(s_lvgl_mtx, t) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void display_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mtx);
}

esp_err_t display_init(const display_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "null cfg");
    s_cfg = *cfg;

    if (cfg->gpio_bl >= 0) {
        gpio_config_t bl = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << cfg->gpio_bl,
        };
        gpio_config(&bl);
        gpio_set_level(cfg->gpio_bl, 0); // off until first frame
    }

    spi_bus_config_t bus = {
        .mosi_io_num     = cfg->gpio_mosi,
        .miso_io_num     = cfg->gpio_miso,
        .sclk_io_num     = cfg->gpio_sclk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = cfg->hres * LVGL_BUFFER_LINES * sizeof(lv_color_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num    = cfg->gpio_dc,
        .cs_gpio_num    = cfg->gpio_cs,
        .pclk_hz        = cfg->pixel_clock_hz,
        .lcd_cmd_bits   = 8,
        .lcd_param_bits = 8,
        .spi_mode       = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx       = &s_disp_drv,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)(intptr_t)cfg->spi_host,
                                                 &io_cfg, &s_panel_io),
                        TAG, "panel_io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = cfg->gpio_rst,
        .color_space    = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel),
                        TAG, "panel_st7789");

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_swap_xy(s_panel, cfg->swap_xy);
    esp_lcd_panel_mirror(s_panel, cfg->mirror_x, cfg->mirror_y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    // LVGL
    lv_init();
    size_t buf_pixels = cfg->hres * LVGL_BUFFER_LINES;
    s_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_buf1 && s_buf2, ESP_ERR_NO_MEM, TAG, "no DMA RAM for LVGL buf");
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = cfg->hres;
    s_disp_drv.ver_res  = cfg->vres;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    s_lvgl_mtx = xSemaphoreCreateRecursiveMutex();

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &s_lvgl_tick_timer),
                        TAG, "tick timer create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000),
                        TAG, "tick timer start");

    if (cfg->gpio_bl >= 0) gpio_set_level(cfg->gpio_bl, 1);

    ESP_LOGI(TAG, "Display initialised %dx%d", cfg->hres, cfg->vres);
    return ESP_OK;
}
