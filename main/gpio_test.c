// Test de pinout J8 — Modo de calibración del cableado.
// Imprime por serial qué GPIO está activo en cada momento; el operador mide
// con multímetro en el header J8 cuál pin sube a 3.3 V.

#include "gpio_test.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gpio_test";

// GPIOs a verificar — los pines que la PCB FA-10T necesita en el header J8.
// Todos válidos en el ESP32-S3 (rangos 0..21 y 26..48).
static const int TEST_PINS[] = { 4, 17, 18, 21 };
#define N_PINS  (sizeof(TEST_PINS) / sizeof(TEST_PINS[0]))

#define ON_DURATION_MS    3000   // tiempo en HIGH para que el multímetro estabilice
#define OFF_DURATION_MS    500   // pausa entre GPIOs

void gpio_test_loop(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "===================================================");
    ESP_LOGW(TAG, "  MODO TEST DE PINOUT — header J8 del Waveshare");
    ESP_LOGW(TAG, "===================================================");
    ESP_LOGW(TAG, "Cada GPIO sube a 3.3 V durante %d s, luego baja.",
             ON_DURATION_MS / 1000);
    ESP_LOGW(TAG, "Con multímetro en modo voltaje DC, tocar cada pin del");
    ESP_LOGW(TAG, "header J8 vs. GND (pin 4) y anotar qué pin sube a 3.3 V");
    ESP_LOGW(TAG, "cuando este log dice ' >>> GPIO_x_ HIGH '.");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Para volver al firmware normal: GPIO_TEST_MODE=0 en");
    ESP_LOGW(TAG, "app_config.h y re-flashear.");
    ESP_LOGW(TAG, "===================================================");
    ESP_LOGW(TAG, "");

    // Configurar todos los GPIOs de la lista como output, sin pull, start LOW.
    gpio_config_t io = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = 0,
    };
    for (size_t i = 0; i < N_PINS; ++i) {
        io.pin_bit_mask |= 1ULL << TEST_PINS[i];
    }
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config FAIL: %s — algún GPIO de la lista es "
                      "inválido en el ESP32-S3 (válidos: 0..21 y 26..48)",
                 esp_err_to_name(err));
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    for (size_t i = 0; i < N_PINS; ++i) gpio_set_level(TEST_PINS[i], 0);

    uint32_t cycle = 0;
    while (1) {
        cycle++;
        ESP_LOGW(TAG, "------------- CICLO %lu -------------",
                 (unsigned long)cycle);

        for (size_t i = 0; i < N_PINS; ++i) {
            const int gpio = TEST_PINS[i];

            ESP_LOGW(TAG, " >>> GPIO%d HIGH (medir AHORA, 3 s)", gpio);
            gpio_set_level(gpio, 1);

            // Imprimir countdown cada segundo para que sepa que sigue ON.
            for (int s = ON_DURATION_MS / 1000; s >= 1; --s) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(TAG, "       GPIO%d HIGH (%d s restante)", gpio, s - 1);
            }

            gpio_set_level(gpio, 0);
            ESP_LOGW(TAG, " <<< GPIO%d LOW", gpio);

            // Pequeña pausa antes del siguiente para que el multímetro vuelva a 0.
            vTaskDelay(pdMS_TO_TICKS(OFF_DURATION_MS));
        }
        ESP_LOGW(TAG, "");   // separar visualmente los ciclos
    }
}
