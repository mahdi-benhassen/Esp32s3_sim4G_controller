#include "b2_ble.h"

#include "b2_buttons.h"
#include "b2_eventlog.h"
#include "b2_settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_B2_BLE_COMMISSIONING_ENABLED
#include "cJSON.h"
#include "esp_random.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_ble";
static uint16_t s_status_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_started;
static bool s_advertising;

static ble_uuid16_t s_service_uuid = BLE_UUID16_INIT(0xFFF0);
static ble_uuid16_t s_command_uuid = BLE_UUID16_INIT(0xFFF1);
static ble_uuid16_t s_status_uuid = BLE_UUID16_INIT(0xFFF2);

static void send_status(const char *text)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_status_handle == 0 || text == NULL) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(text, strlen(text));
    if (om != NULL) {
        int rc = ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "BLE status notification failed: %d", rc);
        }
    }
}

static void generate_http_token(char *token, size_t token_size)
{
    uint8_t random_bytes[24] = {0};
    esp_fill_random(random_bytes, sizeof(random_bytes));
    size_t out = 0;
    for (size_t i = 0; i < sizeof(random_bytes) && out + 2U < token_size; ++i) {
        out += (size_t)snprintf(token + out, token_size - out, "%02x", random_bytes[i]);
    }
}

static bool copy_json_string(const cJSON *root, const char *name, char *destination, size_t destination_size,
                             bool required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (item == NULL) {
        return !required;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= destination_size) {
        return false;
    }
    snprintf(destination, destination_size, "%s", item->valuestring);
    return true;
}

static void reboot_after_provisioning(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(250));
    b2_event_log_flush();
    esp_restart();
}

static int provision_from_json(const char *payload, size_t payload_len)
{
    if (!b2_button_is_pressed(B2_BUTTON_CONFIG) || payload == NULL || payload_len > 768U) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    cJSON *root = cJSON_ParseWithLength(payload, payload_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    const cJSON *operation = cJSON_GetObjectItemCaseSensitive(root, "op");
    if (!cJSON_IsString(operation) || strcmp(operation->valuestring, "provision") != 0) {
        cJSON_Delete(root);
        return BLE_ATT_ERR_UNLIKELY;
    }

    b2_settings_t settings = {0};
    esp_err_t err = b2_settings_load(&settings);
    if (err == ESP_OK) {
        err = copy_json_string(root, "wifi_ssid", settings.wifi_ssid, sizeof(settings.wifi_ssid), true) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "wifi_password", settings.wifi_password, sizeof(settings.wifi_password), true)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "apn", settings.apn, sizeof(settings.apn), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "apn_username", settings.apn_username, sizeof(settings.apn_username), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "apn_password", settings.apn_password, sizeof(settings.apn_password), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "mqtt_uri", settings.mqtt_uri, sizeof(settings.mqtt_uri), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "mqtt_username", settings.mqtt_username, sizeof(settings.mqtt_username), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "mqtt_password", settings.mqtt_password, sizeof(settings.mqtt_password), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && !copy_json_string(root, "mqtt_base_topic", settings.mqtt_base_topic, sizeof(settings.mqtt_base_topic), false)) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK) {
        settings.wifi_enabled = true;
        if (settings.http_auth_token[0] == '\0') {
            generate_http_token(settings.http_auth_token, sizeof(settings.http_auth_token));
        }
        settings.http_auth_required = true;
        err = b2_settings_save(&settings);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    send_status("{\"ok\":true,\"rebooting\":true}\n");
    xTaskCreate(reboot_after_provisioning, "b2_ble_reboot", 3072, NULL, 4, NULL);
    return 0;
}

static int command_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    struct ble_gap_conn_desc desc = {0};
    if (ble_gap_conn_find(conn_handle, &desc) != 0 || !desc.sec_state.encrypted) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length == 0 || length > 768U) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    char payload[769] = {0};
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, length, &copied) != 0 || copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return provision_from_json(payload, length);
}

static int status_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const char *status = b2_button_is_pressed(B2_BUTTON_CONFIG) ?
                         "{\"commissioning\":true,\"presence\":true}\n" :
                         "{\"commissioning\":false,\"presence\":false}\n";
    return os_mbuf_append(ctxt->om, status, strlen(status)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_command_uuid.u,
                .access_cb = command_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = status_access,
                .val_handle = &s_status_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}
        },
    },
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ble_gap_security_initiate(s_conn_handle);
            ESP_LOGI(TAG, "BLE commissioning client connected; encryption requested");
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_advertising = false;
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "BLE commissioning link encrypted");
            send_status("{\"encrypted\":true}\n");
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = false;
        return 0;
    default:
        return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char name[] = "B2-Setup";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.uuids16 = &s_service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) {
        ESP_LOGE(TAG, "BLE advertising fields failed");
        return;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    if (ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL) == 0) {
        s_advertising = true;
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}

static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) == 0) {
        advertise();
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

esp_err_t b2_ble_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (!b2_button_is_pressed(B2_BUTTON_CONFIG)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(s_services) != 0 || ble_gatts_add_svcs(s_services) != 0 ||
        ble_svc_gap_device_name_set("B2-Setup") != 0) {
        nimble_port_deinit();
        return ESP_FAIL;
    }
    s_started = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t b2_ble_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();
    s_started = false;
    s_advertising = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    return ESP_OK;
}

#else

esp_err_t b2_ble_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t b2_ble_stop(void)
{
    return ESP_OK;
}

#endif

esp_err_t b2_ble_get_status(b2_ble_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_B2_BLE_COMMISSIONING_ENABLED
    status->enabled = s_started;
    status->advertising = s_advertising;
#else
    status->enabled = false;
    status->advertising = false;
#endif
    status->physical_presence_required = true;
    return ESP_OK;
}
