#include "app_state.h"

#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "app_state";
#define IDENT_NS "fa10t_id"

static app_state_t      s_state;
static fa10t_config_t   s_config;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t    s_sensor_q;

static void load_identity_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(IDENT_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t sz = MODEL_NAME_MAX;
    esp_err_t err = nvs_get_str(h, "modelo", s_state.modelo, &sz);
    if (err == ESP_OK) ESP_LOGI(TAG, "loaded modelo='%s' from NVS", s_state.modelo);
    sz = SERIE_NUM_MAX;
    err = nvs_get_str(h, "serie", s_state.serie, &sz);
    if (err == ESP_OK) ESP_LOGI(TAG, "loaded serie='%s' from NVS", s_state.serie);
    sz = PIN_LEN_MAX;
    err = nvs_get_str(h, "pin", s_state.pin_servicio, &sz);
    if (err == ESP_OK) ESP_LOGI(TAG, "loaded pin (****) from NVS");
    uint8_t nm = 0;
    if (nvs_get_u8(h, "modulos", &nm) == ESP_OK && nm >= 1) {
        s_state.num_modulos = nm;
        ESP_LOGI(TAG, "loaded num_modulos=%u from NVS", (unsigned)nm);
    }
    nvs_close(h);
}

void app_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.op_mode      = OP_MODE_IDLE;
    s_state.run_state    = RUN_STATE_IDLE;
    s_state.t_min_sesion = 999.0f;   // sentinel; first sample wins
    s_state.t_max_sesion = -999.0f;
    snprintf(s_state.modelo,       MODEL_NAME_MAX, "%s", "IND-26MTO");
    snprintf(s_state.serie,        SERIE_NUM_MAX,  "%s", "-");
    snprintf(s_state.pin_servicio, PIN_LEN_MAX,    "%s", "1234");
    s_state.num_modulos = 1;   // default: 1 turbina + 1 resistencia

    // Si hay valores guardados en NVS, sobreescriben los defaults.
    load_identity_from_nvs();

    s_mtx = xSemaphoreCreateMutex();
    s_sensor_q = xQueueCreate(1, sizeof(sensor_sample_t));
    if (!s_mtx || !s_sensor_q) {
        ESP_LOGE(TAG, "primitives alloc failed (mtx=%p q=%p), restarting", s_mtx, s_sensor_q);
        esp_restart();
    }
}

static esp_err_t save_identity_field(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(IDENT_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_state_save_modelo(const char *modelo)
{
    if (!modelo) return ESP_ERR_INVALID_ARG;
    app_state_lock();
    snprintf(s_state.modelo, MODEL_NAME_MAX, "%s", modelo);
    app_state_unlock();
    esp_err_t err = save_identity_field("modelo", modelo);
    if (err == ESP_OK) ESP_LOGI(TAG, "modelo saved: '%s'", modelo);
    else               ESP_LOGE(TAG, "save modelo failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t app_state_save_serie(const char *serie)
{
    if (!serie) return ESP_ERR_INVALID_ARG;
    app_state_lock();
    snprintf(s_state.serie, SERIE_NUM_MAX, "%s", serie);
    app_state_unlock();
    esp_err_t err = save_identity_field("serie", serie);
    if (err == ESP_OK) ESP_LOGI(TAG, "serie saved: '%s'", serie);
    else               ESP_LOGE(TAG, "save serie failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t app_state_save_pin(const char *pin)
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    app_state_lock();
    snprintf(s_state.pin_servicio, PIN_LEN_MAX, "%s", pin);
    app_state_unlock();
    esp_err_t err = save_identity_field("pin", pin);
    if (err == ESP_OK) ESP_LOGI(TAG, "pin saved");
    else               ESP_LOGE(TAG, "save pin failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t app_state_save_num_modulos(uint8_t n)
{
    if (n < 1)            n = 1;
    if (n > MODULOS_MAX)  n = MODULOS_MAX;
    app_state_lock();
    s_state.num_modulos = n;
    app_state_unlock();

    nvs_handle_t h;
    esp_err_t err = nvs_open(IDENT_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "modulos", n);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "num_modulos saved: %u", (unsigned)n);
    else               ESP_LOGE(TAG, "save num_modulos failed: %s", esp_err_to_name(err));
    return err;
}

void app_state_factory_reset(void)
{
    // Preservar identidad de equipo (se re-escribe tras borrar). El dev_id/token
    // de nube viven en OTRA partición ("factory") y NO se tocan acá.
    char modelo[MODEL_NAME_MAX], serie[SERIE_NUM_MAX];
    uint8_t num_mod;
    app_state_lock();
    snprintf(modelo, sizeof(modelo), "%s", s_state.modelo);
    snprintf(serie,  sizeof(serie),  "%s", s_state.serie);
    num_mod = s_state.num_modulos;
    app_state_unlock();

    ESP_LOGW(TAG, "RESET TOTAL: borrando NVS por defecto (identidad y nube se conservan)…");
    esp_err_t err = nvs_flash_erase();          // sólo la partición "nvs"; "factory" intacta
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_flash_erase: %s", esp_err_to_name(err));
    err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));

    // Re-escribir la identidad preservada en la NVS recién borrada.
    app_state_save_modelo(modelo);
    app_state_save_serie(serie);
    app_state_save_num_modulos(num_mod);

    ESP_LOGW(TAG, "RESET TOTAL completo (modelo='%s' serie='%s'). Reiniciando…", modelo, serie);
    esp_restart();                               // no retorna
}

QueueHandle_t app_state_sensor_queue(void) { return s_sensor_q; }
void app_state_lock(void)                  { xSemaphoreTake(s_mtx, portMAX_DELAY); }
void app_state_unlock(void)                { xSemaphoreGive(s_mtx); }
app_state_t *app_state_get(void)           { return &s_state; }

void app_state_copy_config(fa10t_config_t *out)
{
    if (!out) return;
    app_state_lock();
    *out = s_config;
    app_state_unlock();
}

void app_state_set_config(const fa10t_config_t *cfg)
{
    if (!cfg) return;
    app_state_lock();
    s_config = *cfg;
    s_state.setpoint = cfg->setpoint;
    app_state_unlock();
}
