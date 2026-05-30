#include "audio.h"
#include "app_config.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#if !SIMULATION_MODE
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "display.h"
#include "sht31.h"   // shared I2C bus
#endif

static const char *TAG = "audio";

// --- Pinout I2S del Waveshare-3.5 (ver display_pins.h / schematic) ---
#define I2S_PIN_MCLK   12
#define I2S_PIN_BCLK   13
#define I2S_PIN_WS     15   // LRCK
#define I2S_PIN_DOUT   16   // → DSDIN del codec
#define I2S_PIN_DIN    (-1) // no usamos capture

#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_TASK_STACK    4096
#define AUDIO_TASK_PRIO     3
#define AUDIO_QUEUE_LEN     4

typedef struct {
    uint16_t freq_hz;
    uint16_t dur_ms;
    uint8_t  n_repeats;     // 1 para single beep, >1 para secuencia
    uint16_t gap_ms;        // silencio entre repeticiones (sólo si n_repeats > 1)
    uint8_t  amp_pct;       // amplitud 0..100. clicks usan ~40, alarma 100.
    bool     square_wave;   // true = onda cuadrada (más fuerte y "alarmante")
} beep_req_t;

static QueueHandle_t s_q;
static bool          s_ready;
#if !SIMULATION_MODE
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t      s_tx_chan;
#endif

#if !SIMULATION_MODE
// --- Generador de tono con envelope de 2 ms ramp-in / ramp-out ---
static void play_tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t amp_pct,
                      bool square)
{
    if (freq_hz < 100)  freq_hz = 100;
    if (freq_hz > 8000) freq_hz = 8000;
    if (dur_ms  < 5)    dur_ms  = 5;
    if (dur_ms  > 600)  dur_ms  = 600;
    if (amp_pct > 100)  amp_pct = 100;

    const uint32_t total_samples = (uint32_t)AUDIO_SAMPLE_RATE * dur_ms / 1000u;
    const uint32_t ramp_samples  = AUDIO_SAMPLE_RATE * 2u / 1000u;   // 2 ms

    int16_t *buf = malloc(total_samples * sizeof(int16_t));
    if (!buf) {
        ESP_LOGW(TAG, "play_tone: malloc %lu failed", (unsigned long)(total_samples * 2));
        return;
    }

    const float omega = 2.0f * (float)M_PI * (float)freq_hz / (float)AUDIO_SAMPLE_RATE;
    const float peak  = 32760.0f * (float)amp_pct / 100.0f;
    for (uint32_t i = 0; i < total_samples; ++i) {
        float env = 1.0f;
        if (i < ramp_samples)                 env = (float)i / (float)ramp_samples;
        if (i + ramp_samples > total_samples) env = (float)(total_samples - i) / (float)ramp_samples;
        if (env < 0.0f) env = 0.0f;
        if (env > 1.0f) env = 1.0f;
        float s = sinf(omega * (float)i);
        // Square: solo el signo de la sinusoidal → onda cuadrada. RMS = amp
        // completa (sine RMS = amp / √2), o sea ~41% más fuerte percibido.
        if (square) s = (s >= 0.0f) ? 1.0f : -1.0f;
        buf[i] = (int16_t)(s * peak * env);
    }

    esp_codec_dev_write(s_codec, buf, (int)(total_samples * sizeof(int16_t)));
    free(buf);
}

static void audio_task(void *arg)
{
    (void)arg;
    beep_req_t req;
    while (1) {
        if (xQueueReceive(s_q, &req, portMAX_DELAY) == pdTRUE) {
            uint8_t n = req.n_repeats == 0 ? 1 : req.n_repeats;
            if (n > 8) n = 8;
            uint8_t amp = req.amp_pct == 0 ? 50 : req.amp_pct;
            for (uint8_t i = 0; i < n; ++i) {
                play_tone(req.freq_hz, req.dur_ms, amp, req.square_wave);
                if (i + 1 < n && req.gap_ms > 0) {
                    vTaskDelay(pdMS_TO_TICKS(req.gap_ms));
                }
            }
        }
    }
}
#endif

esp_err_t audio_init(void)
{
    if (s_ready) return ESP_OK;

    s_q = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(beep_req_t));
    if (!s_q) return ESP_ERR_NO_MEM;

#if SIMULATION_MODE
    s_ready = true;
    ESP_LOGI(TAG, "audio init [SIMULATION_MODE]");
    return ESP_OK;
#else
    // 1) I2S master TX channel.
    // auto_clear=true → cuando se acaba el buffer activo, el DMA emite ceros
    // (silencio) en lugar de repetir el último frame. Sin esto, el tono
    // siguió loopeando después del beep y se aceleraba al meter nuevos.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s_new_channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_PIN_MCLK,
            .bclk = I2S_PIN_BCLK,
            .ws   = I2S_PIN_WS,
            .dout = I2S_PIN_DOUT,
            .din  = I2S_PIN_DIN,
            .invert_flags = { 0 },
        },
    };
    // MCLK = 256 × sample_rate (estándar para ES8311).
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg),
                        TAG, "i2s_init_std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s_enable");

    // 2) Control I/F = I2C (bus compartido del SHT31).
    i2c_master_bus_handle_t bus = sht31_shared_bus();
    if (!bus) { ESP_LOGE(TAG, "I2C bus not ready"); return ESP_ERR_INVALID_STATE; }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port      = 0,           // no se usa cuando bus_handle != NULL
        .addr      = ES8311_CODEC_DEFAULT_ADDR,   // 0x30 (7-bit shifted)
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) { ESP_LOGE(TAG, "codec ctrl_if create failed"); return ESP_FAIL; }

    // 3) Data I/F = I2S TX.
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port      = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (!data_if) { ESP_LOGE(TAG, "codec data_if create failed"); return ESP_FAIL; }

    // 4) Codec ES8311. pa_pin=-1 porque el amp está detrás del TCA9554 (lo
    //    encendemos manualmente con display_set_pa_amp() abajo).
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if  = ctrl_if,
        .gpio_if  = NULL,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,        // codec es slave del I2S, ESP es master
        .pa_pin = -1,
        .pa_reverted = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = { 0 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if  = codec_if,
        .data_if   = data_if,
        .dev_type  = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    esp_codec_dev_sample_info_t fs_info = {
        .sample_rate     = AUDIO_SAMPLE_RATE,
        .channel         = 1,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_codec, &fs_info), TAG, "codec_open");
    esp_codec_dev_set_out_vol(s_codec, 100);   // 0..100, máximo

    // 5) Encender el amplificador NS4150 (EXIO7 del TCA9554).
    display_set_pa_amp(true);

    // 6) Task que consume requests de la cola.
    BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio",
                                            AUDIO_TASK_STACK, NULL,
                                            AUDIO_TASK_PRIO, NULL, 0);
    if (ok != pdPASS) { ESP_LOGE(TAG, "task create failed"); return ESP_ERR_NO_MEM; }

    s_ready = true;
    ESP_LOGI(TAG, "audio ready: ES8311 @ 16kHz/16b mono, vol 50%%");
    return ESP_OK;
#endif
}

void audio_click(void)
{
    if (!s_ready) return;
    // Click suave: el volumen del codec está fijo en 100% para que la alarma
    // de fin de ciclo suene fuerte. Para que los clicks de teclado no
    // abrumen, usamos amplitud baja en el tono mismo (15% del peak).
    beep_req_t r = { .freq_hz = 2000, .dur_ms = 25,
                     .n_repeats = 1, .gap_ms = 0, .amp_pct = 15 };
    (void)xQueueSend(s_q, &r, 0);
}

void audio_beep(uint16_t freq_hz, uint16_t dur_ms)
{
    if (!s_ready) return;
    beep_req_t r = { .freq_hz = freq_hz, .dur_ms = dur_ms,
                     .n_repeats = 1, .gap_ms = 0, .amp_pct = 70 };
    (void)xQueueSend(s_q, &r, 0);
}

void audio_beep_seq(uint16_t freq_hz, uint16_t dur_ms,
                     uint8_t n_repeats, uint16_t gap_ms)
{
    if (!s_ready) return;
    beep_req_t r = { .freq_hz = freq_hz, .dur_ms = dur_ms,
                     .n_repeats = n_repeats, .gap_ms = gap_ms,
                     .amp_pct = 70 };
    (void)xQueueSend(s_q, &r, 0);
}

void audio_alarm_done(void)
{
    // 5 beeps de 450 ms a 1 kHz con 180 ms de silencio entre cada uno.
    // ONDA CUADRADA + amplitud máxima + volumen codec al 100% → tan fuerte
    // como permite el amplificador NS4150B del board. ~+3 dB RMS frente a
    // sine, y armónicos altos que el oído percibe como "agudo y alarmante".
    if (!s_ready) return;
    beep_req_t r = { .freq_hz = 1000, .dur_ms = 450,
                     .n_repeats = 5, .gap_ms = 180,
                     .amp_pct = 100, .square_wave = true };
    (void)xQueueSend(s_q, &r, 0);
}

void audio_set_volume(uint8_t percent)
{
    if (!s_ready) return;
#if !SIMULATION_MODE
    if (percent > 100) percent = 100;
    esp_codec_dev_set_out_vol(s_codec, percent);
#else
    (void)percent;
#endif
}
