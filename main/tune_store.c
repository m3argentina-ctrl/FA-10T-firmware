#include "tune_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "tune_store";

#define TUNE_PART  "factory"
#define TUNE_NS    "fa10t_tune"

typedef struct { float kp, ki, kd; } pid_blob_t;

static bool s_ok;

esp_err_t tune_store_init(void)
{
    esp_err_t err = nvs_flash_init_partition(TUNE_PART);   // idempotente: cloud_telemetry la vuelve a abrir
    s_ok = (err == ESP_OK);
    if (!s_ok) {
        ESP_LOGW(TAG, "partición '%s' no disponible (%s): autotune y sondas sin persistencia",
                 TUNE_PART, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (!s_ok) return ESP_ERR_INVALID_STATE;
    return nvs_open_from_partition(TUNE_PART, TUNE_NS, mode, h);
}

bool tune_store_load_pid(float *kp, float *ki, float *kd)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return false;
    pid_blob_t b;
    size_t sz = sizeof b;
    esp_err_t err = nvs_get_blob(h, "pid", &b, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof b) return false;
    if (!(b.kp > 0.0f) || !(b.ki >= 0.0f) || !(b.kd >= 0.0f)) return false;   // también descarta NaN
    *kp = b.kp;
    *ki = b.ki;
    *kd = b.kd;
    return true;
}

esp_err_t tune_store_save_pid(float kp, float ki, float kd)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    const pid_blob_t b = { kp, ki, kd };
    err = nvs_set_blob(h, "pid", &b, sizeof b);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "PID autotune guardado: Kp=%.4f Ki=%.6f Kd=%.3f (%s)",
             kp, ki, kd, esp_err_to_name(err));
    return err;
}

esp_err_t tune_store_clear_pid(void)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, "pid");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint64_t tune_store_load_heater_rom(void)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return 0;
    uint64_t rom = 0;
    if (nvs_get_u64(h, "heat_rom", &rom) != ESP_OK) rom = 0;
    nvs_close(h);
    return rom;
}

esp_err_t tune_store_save_heater_rom(uint64_t rom)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (rom == 0) {
        err = nvs_erase_key(h, "heat_rom");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_u64(h, "heat_rom", rom);
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "sonda de resistencias: ROM=0x%016llX (%s)",
             (unsigned long long)rom, esp_err_to_name(err));
    return err;
}
