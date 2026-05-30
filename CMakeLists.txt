# FA-10T v3.0 — Industrial Thermal Controller
# Target: ESP32-S3
# Build system: ESP-IDF

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fa10t_firmware)

# --------------------------------------------------------------------------
# Mover el pool estatico de LVGL (LV_MEM_SIZE_KILOBYTES = 128KB) de la DRAM
# interna a la PSRAM. En este board (ESP32-S3 N8R8) la DRAM interna es el
# recurso critico: sin alivio el driver WiFi apenas inicializa ("malloc buffer
# fail", "Expected 10 rx buffer, actual 8") y el handshake TLS queda sin RAM.
# El pool se declara en lv_mem.c como:
#     static LV_ATTRIBUTE_LARGE_RAM_ARRAY MEM_UNIT work_mem_int[...];
# Definiendo ese atributo como la seccion .ext_ram.bss, el array de 128KB pasa
# a PSRAM y libera 128KB de DRAM interna para WiFi + TLS. lv_conf_internal.h
# respeta el #ifndef, asi que esta -D gana sobre el default vacio del Kconfig.
# Requiere CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (ver sdkconfig.defaults).
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE
    "LV_ATTRIBUTE_LARGE_RAM_ARRAY=__attribute__((section(\".ext_ram.bss\")))")
