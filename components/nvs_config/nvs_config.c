#include "nvs_config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_cfg";
static const char *KEY = "blob";

void nvs_config_defaults(fa10t_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->version          = NVS_CONFIG_VERSION;
    c->kp               = 8.0f;
    c->ki               = 0.15f;
    c->kd               = 40.0f;
    c->setpoint         = 60.0f;
    c->temp_max_c       = 250.0f;
    c->runaway_dt_c     = 2.0f;
    c->runaway_window_s = 90.0f;
    c->cal_gain         = 1.0f;
    c->cal_offset       = 0.0f;
    c->ssr_period_ms    = 1000;
}

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s) — reformatting", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_config_load(fa10t_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved config (%s), using defaults", esp_err_to_name(err));
        nvs_config_defaults(out);
        return ESP_OK;
    }

    size_t sz = sizeof(*out);
    err = nvs_get_blob(h, KEY, out, &sz);
    nvs_close(h);

    if (err != ESP_OK || sz != sizeof(*out) || out->version != NVS_CONFIG_VERSION) {
        ESP_LOGW(TAG, "config blob missing/invalid (err=%s sz=%u ver=%u), using defaults",
                 esp_err_to_name(err), (unsigned)sz, (unsigned)out->version);
        nvs_config_defaults(out);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "config loaded: SP=%.1f°C Kp=%.2f Ki=%.3f Kd=%.2f",
             out->setpoint, out->kp, out->ki, out->kd);
    return ESP_OK;
}

esp_err_t nvs_config_save(const fa10t_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &h), TAG, "open");

    esp_err_t err = nvs_set_blob(h, KEY, cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) ESP_LOGI(TAG, "config saved");
    else               ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t nvs_config_erase(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
