#include "b2_settings.h"

#include "esp_check.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "b2_settings";
static const char *NVS_NAMESPACE = "b2cfg";
static const char *NVS_KEY = "settings";
static const uint32_t SETTINGS_VERSION = 1;

static void set_defaults(b2_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->version = SETTINGS_VERSION;
    settings->restore_relay_state = false;
    settings->sms_auth_mode = B2_SMS_AUTH_ALLOW_ALL;
    settings->sms_allowlist_count = 0;
    // Empty APN means that the modem/network layer must be configured later.
    settings->apn[0] = '\0';
    settings->mqtt_uri[0] = '\0';
    settings->mqtt_enabled = false;
    settings->mqtt_username[0] = '\0';
    settings->mqtt_password[0] = '\0';
    snprintf(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic), "b2/controller");
    settings->wifi_enabled = false;
    settings->wifi_ssid[0] = '\0';
    settings->wifi_password[0] = '\0';
}

static esp_err_t validate(const b2_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG, "null settings");
    ESP_RETURN_ON_FALSE(settings->version == SETTINGS_VERSION, ESP_ERR_INVALID_VERSION, TAG, "unsupported settings version");
    ESP_RETURN_ON_FALSE(settings->sms_auth_mode <= B2_SMS_AUTH_ALLOWLIST, ESP_ERR_INVALID_ARG, TAG, "invalid SMS auth mode");
    ESP_RETURN_ON_FALSE(settings->sms_allowlist_count <= B2_SETTINGS_SMS_ALLOWLIST_MAX, ESP_ERR_INVALID_ARG, TAG, "too many SMS numbers");
    for (size_t i = 0; i < B2_SETTINGS_SMS_ALLOWLIST_MAX; ++i) {
        ESP_RETURN_ON_FALSE(strnlen(settings->sms_allowlist[i], B2_SETTINGS_PHONE_MAX) < B2_SETTINGS_PHONE_MAX,
                            ESP_ERR_INVALID_SIZE, TAG, "SMS number is not terminated");
    }
    ESP_RETURN_ON_FALSE(strnlen(settings->apn, B2_SETTINGS_APN_MAX) < B2_SETTINGS_APN_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "APN is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->mqtt_uri, B2_SETTINGS_MQTT_URI_MAX) < B2_SETTINGS_MQTT_URI_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "MQTT URI is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->mqtt_username, B2_SETTINGS_MQTT_USERNAME_MAX) < B2_SETTINGS_MQTT_USERNAME_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "MQTT username is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->mqtt_password, B2_SETTINGS_MQTT_PASSWORD_MAX) < B2_SETTINGS_MQTT_PASSWORD_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "MQTT password is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->mqtt_base_topic, B2_SETTINGS_MQTT_TOPIC_MAX) < B2_SETTINGS_MQTT_TOPIC_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "MQTT topic is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->wifi_ssid, B2_SETTINGS_WIFI_SSID_MAX) < B2_SETTINGS_WIFI_SSID_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "Wi-Fi SSID is not terminated");
    ESP_RETURN_ON_FALSE(strnlen(settings->wifi_password, B2_SETTINGS_WIFI_PASSWORD_MAX) < B2_SETTINGS_WIFI_PASSWORD_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "Wi-Fi password is not terminated");
    return ESP_OK;
}

esp_err_t b2_settings_load(b2_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG, "null settings output");
    set_defaults(settings);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open settings namespace");

    size_t length = sizeof(*settings);
    err = nvs_get_blob(handle, NVS_KEY, settings, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND || length != sizeof(*settings)) {
        set_defaults(settings);
        return ESP_OK;
    }
    if (err != ESP_OK || validate(settings) != ESP_OK) {
        set_defaults(settings);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t b2_settings_save(const b2_settings_t *settings)
{
    ESP_RETURN_ON_ERROR(validate(settings), TAG, "validate settings");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open settings namespace");
    esp_err_t err = nvs_set_blob(handle, NVS_KEY, settings, sizeof(*settings));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t b2_settings_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open settings namespace");
    err = nvs_erase_key(handle, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
