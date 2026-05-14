idf_component_register(
    SRCS "safety.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_system esp_timer log ssr_driver
)
