#include "ble_monitor.h"
#include "app_config.h"

#include <string.h>

#include "esp_log.h"

#if BLE_ENABLED
// IMPORTANTE: para que esto realmente compile y enlace, además de
// BLE_ENABLED=1 hay que activar en sdkconfig:
//   CONFIG_BT_ENABLED=y
//   CONFIG_BT_NIMBLE_ENABLED=y
// Hacelo con `idf.py menuconfig` → Component config → Bluetooth → enable +
// → Bluetooth Host: NimBLE. Sin esto el bt component no expone los headers
// y el build falla con "Missing nimble_port.h".

#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_mon";

// --- UUIDs custom 16-bit (rango 0xFAxx) ---
#define UUID_SVC_FA10T          0xFA10
#define UUID_CHR_TEMP_ACTUAL    0xFA11
#define UUID_CHR_SETPOINT       0xFA12
#define UUID_CHR_ESTADO         0xFA13
#define UUID_CHR_TIEMPO_RES     0xFA14

static uint8_t  s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_chr_estado_handle;
static uint16_t s_chr_tiempo_handle;

// Estado publicado (modificado desde otros tasks via las API publish_*)
static float    s_temp_c        = 0.0f;
static float    s_setpoint_c    = 0.0f;
static char     s_estado[24]    = "IDLE";
static uint32_t s_tiempo_res_s  = 0;

static void start_advertising(void);

// --- GATT access callbacks ---
static int gatt_access_temp(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_temp_c, sizeof(s_temp_c)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_access_setpoint(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_setpoint_c, sizeof(s_setpoint_c)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(float)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        ble_hs_mbuf_to_flat(ctxt->om, &s_setpoint_c, sizeof(s_setpoint_c), NULL);
        ESP_LOGI(TAG, "client wrote SP=%.1f°C", s_setpoint_c);
        // TODO: enrutar al modo_manual/programa cuando se quiera permitir
        // cambiar el setpoint desde BLE.
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_access_estado(uint16_t conn, uint16_t attr,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_estado, strlen(s_estado)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_access_tiempo(uint16_t conn, uint16_t attr,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_tiempo_res_s, sizeof(s_tiempo_res_s)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// --- GATT service definition ---
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_SVC_FA10T),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid       = BLE_UUID16_DECLARE(UUID_CHR_TEMP_ACTUAL),
              .access_cb  = gatt_access_temp,
              .flags      = BLE_GATT_CHR_F_READ },
            { .uuid       = BLE_UUID16_DECLARE(UUID_CHR_SETPOINT),
              .access_cb  = gatt_access_setpoint,
              .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
            { .uuid       = BLE_UUID16_DECLARE(UUID_CHR_ESTADO),
              .access_cb  = gatt_access_estado,
              .val_handle = &s_chr_estado_handle,
              .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            { .uuid       = BLE_UUID16_DECLARE(UUID_CHR_TIEMPO_RES),
              .access_cb  = gatt_access_tiempo,
              .val_handle = &s_chr_tiempo_handle,
              .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            { 0 }, // terminator
        },
    },
    { 0 }, // terminator
};

// --- GAP events ---
static int gap_event(struct ble_gap_event *evt, void *arg)
{
    (void)arg;
    switch (evt->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "GAP connect: status=%d", evt->connect.status);
        if (evt->connect.status == 0) {
            s_conn_handle = evt->connect.conn_handle;
        } else {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "GAP disconnect: reason=%d", evt->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags          = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl     = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    const char *name = BLE_DEVICE_NAME_DEFAULT;
    fields.name           = (uint8_t *)name;
    fields.name_len       = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) { ESP_LOGW(TAG, "adv_set_fields=%d", rc); return; }
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc) ESP_LOGW(TAG, "adv_start=%d", rc);
    else    ESP_LOGI(TAG, "advertising '%s'", name);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_addr_type);
    start_advertising();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_monitor_init(void)
{
    int rc = nimble_port_init();    // returns int in ESP-IDF v6
    if (rc != 0) { ESP_LOGE(TAG, "nimble_port_init=%d", rc); return ESP_FAIL; }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc) { ESP_LOGE(TAG, "gatts_count_cfg=%d", rc); return ESP_FAIL; }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc) { ESP_LOGE(TAG, "gatts_add_svcs=%d", rc); return ESP_FAIL; }

    ble_svc_gap_device_name_set(BLE_DEVICE_NAME_DEFAULT);
    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE ready, advertising as '%s'", BLE_DEVICE_NAME_DEFAULT);
    return ESP_OK;
}

// --- Publishers (notifican si hay cliente conectado) ---
static void notify(uint16_t handle, const void *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) ble_gatts_notify_custom(s_conn_handle, handle, om);
}

void ble_monitor_publish_temperature(float t_c)
{
    s_temp_c = t_c;
}

void ble_monitor_publish_setpoint(float sp_c)
{
    s_setpoint_c = sp_c;
}

void ble_monitor_publish_estado(const char *txt)
{
    if (!txt) return;
    strlcpy(s_estado, txt, sizeof(s_estado));
    notify(s_chr_estado_handle, s_estado, strlen(s_estado));
}

void ble_monitor_publish_tiempo_restante(uint32_t seconds)
{
    s_tiempo_res_s = seconds;
    notify(s_chr_tiempo_handle, &s_tiempo_res_s, sizeof(s_tiempo_res_s));
}

bool ble_monitor_is_advertising(void) { return s_conn_handle == BLE_HS_CONN_HANDLE_NONE; }
bool ble_monitor_has_client(void)     { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

#else  /* BLE_ENABLED == 0 — stubs */

esp_err_t ble_monitor_init(void)                                  { return ESP_OK; }
void ble_monitor_publish_temperature(float t)                     { (void)t; }
void ble_monitor_publish_setpoint(float sp)                       { (void)sp; }
void ble_monitor_publish_estado(const char *t)                    { (void)t; }
void ble_monitor_publish_tiempo_restante(uint32_t s)              { (void)s; }
bool ble_monitor_is_advertising(void)                             { return false; }
bool ble_monitor_has_client(void)                                 { return false; }

#endif /* BLE_ENABLED */
