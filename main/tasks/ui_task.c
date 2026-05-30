#include "ui_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"
#include "display.h"
#include "audio.h"
#include "screens/ui_common.h"
#include "screens/recovery_prompt.h"
#include "recovery.h"

static const char *TAG = "ui_task";

// Auto-atenuado por inactividad: tras AUTO_DIM_MS sin tocar la pantalla bajamos
// el backlight a su nivel de reposo (display_backlight_set_dimmed). Valor fijo
// en firmware. El primer toque resetea el idle de LVGL y volvemos al brillo del
// operario. La lógica vive acá porque necesita lv_disp_get_inactive_time().
#define AUTO_DIM_MS  (2u * 60u * 1000u)   // 2 minutos

static TaskHandle_t s_handle;
TaskHandle_t ui_task_handle(void) { return s_handle; }

// Brief serial dump every ~2s for non-UI debugging.
static void serial_debug(void)
{
    static uint64_t last_us;
    uint64_t now = esp_timer_get_time();
    if (now - last_us < 2000000ULL) return;
    last_us = now;

    app_state_lock();
    const app_state_t s = *app_state_get();
    app_state_unlock();
    ESP_LOGI(TAG,
             "T=%.1f SP=%.1f drv=%.0f%% fan=%.0f%% hum=%.1f%% st=%d wu=%d el=%lus rem=%lus",
             s.last_sample.temperature, s.effective_setpoint,
             s.ssr_drv_duty * 100, s.ssr_fan_duty * 100,
             s.humidity, (int)s.run_state, (int)s.warmup_done,
             (unsigned long)s.session_elapsed_s,
             (unsigned long)s.session_remaining_s);
}

static void ui_task(void *arg)
{
    (void)arg;
    safety_wdt_subscribe();

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display_init failed — UI stays inert");
    }
    if (audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio_init failed — UI sin sonido");
    }
    ui_common_init();

    // Power-fail recovery: si el último corte dejó una sesión corriendo,
    // ofrecer recuperarla con un modal de cuenta regresiva (auto-reanuda si
    // nadie responde — ver recovery_prompt.c). El modal vive en lv_layer_top()
    // por encima del splash y maneja él mismo la navegación al cerrarse.
    recovery_snapshot_t snap;
    if (recovery_has_session(&snap)) {
        ESP_LOGW(TAG, "sesión interrumpida detectada: '%s' mode=%d stage=%u t=%lus — ofreciendo recuperar",
                 snap.recipe.nombre, (int)snap.op_mode, snap.etapa_activa,
                 (unsigned long)snap.session_elapsed_s);
        recovery_prompt_show(&snap);
    }

    // Encender el backlight al brillo guardado (NVS) recién ahora que LVGL ya
    // volcó el primer frame — evita mostrar basura del panel al arranque.
    display_set_backlight(display_get_backlight());

    while (1) {
        uint32_t idle_ms = 0;
        if (display_lvgl_lock(20)) {
            lv_timer_handler();
            idle_ms = lv_disp_get_inactive_time(NULL);   // ms desde el último touch
            display_lvgl_unlock();
            // Atenuar/restaurar fuera del lock (no es llamada LVGL). La función es
            // idempotente, así que llamarla cada ciclo no reescribe el PWM.
            display_backlight_set_dimmed(idle_ms >= AUTO_DIM_MS);
        }
        serial_debug();
        safety_wdt_feed();
        // 20 → 10 ms: en pantallas con update_cb pesado (FUNC_PROGRAMAS dirtea
        // ~25 objetos cada 500 ms) un solo lv_timer_handler() puede tardar 40-
        // 80 ms entre render + flush DMA. Con vTaskDelay(20) el polling de touch
        // efectivo bajaba a ~60-100 ms y se perdían tactos. Con 10 ms le damos
        // a LVGL más oportunidades de procesar el input timer (15 ms interno).
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_task_start(void)
{
    xTaskCreatePinnedToCore(ui_task, "ui",
                            UI_TASK_STACK, NULL,
                            UI_TASK_PRIO, &s_handle,
                            UI_TASK_CORE);
    ESP_LOGI(TAG, "UI task started");
}
