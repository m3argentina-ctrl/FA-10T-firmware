idf_component_register(
    SRCS "max31865.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common esp_timer log
)
