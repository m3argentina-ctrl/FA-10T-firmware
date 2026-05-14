idf_component_register(
    SRCS
        "display.c"
        "ui_screens.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer log
    PRIV_REQUIRES lvgl__lvgl
)
