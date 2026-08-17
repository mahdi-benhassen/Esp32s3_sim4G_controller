#include "b2_settings.h"

#include "esp_check.h"
#include "nvs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_settings";
static const char *NVS_NAMESPACE = "b2cfg";
static const char *NVS_KEY = "settings";
static const uint32_t SETTINGS_VERSION = 4;

typedef struct {
    uint32_t version;
    bool restore_relay_state;
    bool relay_state[2];
    b2_sms_auth_mode_t sms_auth_mode;
    uint8_t sms_allowlist_count;
    char sms_allowlist[B2_SETTINGS_SMS_ALLOWLIST_MAX][B2_SETTINGS_PHONE_MAX];
    char apn[B2_SETTINGS_APN_MAX];
    char mqtt_uri[B2_SETTINGS_MQTT_URI_MAX];
    bool mqtt_enabled;
    char mqtt_username[B2_SETTINGS_MQTT_USERNAME_MAX];
    char mqtt_password[B2_SETTINGS_MQTT_PASSWORD_MAX];
    char mqtt_base_topic[B2_SETTINGS_MQTT_TOPIC_MAX];
    bool wifi_enabled;
    char wifi_ssid[B2_SETTINGS_WIFI_SSID_MAX];
    char wifi_password[B2_SETTINGS_WIFI_PASSWORD_MAX];
} b2_settings_v1_t;

typedef struct {
    uint32_t version;
    bool restore_relay_state;
    bool relay_state[2];
    bool relay_interlock;
    bool relay_fail_safe_off;
    b2_sms_auth_mode_t sms_auth_mode;
    uint8_t sms_allowlist_count;
    char sms_allowlist[B2_SETTINGS_SMS_ALLOWLIST_MAX][B2_SETTINGS_PHONE_MAX];
    uint32_t sms_rate_limit_seconds;
    char sms_shared_secret[B2_SETTINGS_SMS_SECRET_MAX];
    char apn[B2_SETTINGS_APN_MAX];
    char apn_username[B2_SETTINGS_APN_USERNAME_MAX];
    char apn_password[B2_SETTINGS_APN_PASSWORD_MAX];
    char mqtt_uri[B2_SETTINGS_MQTT_URI_MAX];
    bool mqtt_enabled;
    bool mqtt_allow_plaintext;
    char mqtt_username[B2_SETTINGS_MQTT_USERNAME_MAX];
    char mqtt_password[B2_SETTINGS_MQTT_PASSWORD_MAX];
    char mqtt_ca_certificate[B2_SETTINGS_MQTT_CA_MAX];
    char mqtt_base_topic[B2_SETTINGS_MQTT_TOPIC_MAX];
    bool wifi_enabled;
    char wifi_ssid[B2_SETTINGS_WIFI_SSID_MAX];
    char wifi_password[B2_SETTINGS_WIFI_PASSWORD_MAX];
    char http_auth_token[B2_SETTINGS_HTTP_TOKEN_MAX];
    bool http_auth_required;
    bool time_sync_enabled;
    char timezone[B2_SETTINGS_TIMEZONE_MAX];
    float analog_gain[B2_ANALOG_CALIBRATION_COUNT];
    float analog_offset[B2_ANALOG_CALIBRATION_COUNT];
    uint8_t rule_count;
    b2_rule_t rules[B2_SETTINGS_RULE_COUNT];
} b2_settings_v2_t;

typedef struct {
    uint32_t version;
    bool restore_relay_state;
    bool relay_state[2];
    bool relay_interlock;
    bool relay_fail_safe_off;
    b2_sms_auth_mode_t sms_auth_mode;
    uint8_t sms_allowlist_count;
    char sms_allowlist[B2_SETTINGS_SMS_ALLOWLIST_MAX][B2_SETTINGS_PHONE_MAX];
    uint32_t sms_rate_limit_seconds;
    char sms_shared_secret[B2_SETTINGS_SMS_SECRET_MAX];
    char apn[B2_SETTINGS_APN_MAX];
    char apn_username[B2_SETTINGS_APN_USERNAME_MAX];
    char apn_password[B2_SETTINGS_APN_PASSWORD_MAX];
    b2_apn_auth_type_t apn_auth_type;
    char mqtt_uri[B2_SETTINGS_MQTT_URI_MAX];
    bool mqtt_enabled;
    bool mqtt_allow_plaintext;
    char mqtt_username[B2_SETTINGS_MQTT_USERNAME_MAX];
    char mqtt_password[B2_SETTINGS_MQTT_PASSWORD_MAX];
    char mqtt_ca_certificate[B2_SETTINGS_MQTT_CA_MAX];
    char mqtt_base_topic[B2_SETTINGS_MQTT_TOPIC_MAX];
    bool wifi_enabled;
    char wifi_ssid[B2_SETTINGS_WIFI_SSID_MAX];
    char wifi_password[B2_SETTINGS_WIFI_PASSWORD_MAX];
    char http_auth_token[B2_SETTINGS_HTTP_TOKEN_MAX];
    bool http_auth_required;
    bool time_sync_enabled;
    char timezone[B2_SETTINGS_TIMEZONE_MAX];
    float analog_gain[B2_ANALOG_CALIBRATION_COUNT];
    float analog_offset[B2_ANALOG_CALIBRATION_COUNT];
    uint8_t rule_count;
    b2_rule_t rules[B2_SETTINGS_RULE_COUNT];
} b2_settings_v3_t;

static void set_defaults(b2_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->version = SETTINGS_VERSION;
    settings->sms_auth_mode = B2_SMS_AUTH_ALLOWLIST;
    settings->sms_rate_limit_seconds = 30;
    settings->relay_fail_safe_off = true;
    settings->mqtt_allow_plaintext = false;
    settings->apn_auth_type = B2_APN_AUTH_NONE;
    settings->http_auth_required = false;
    settings->time_sync_enabled = true;
    snprintf(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic), "b2/controller");
    snprintf(settings->timezone, sizeof(settings->timezone), "UTC0");
    snprintf(settings->sntp_server, sizeof(settings->sntp_server), "pool.ntp.org");
    for (size_t i = 0; i < B2_ANALOG_CALIBRATION_COUNT; ++i) {
        settings->analog_gain[i] = 1.0f;
        settings->analog_offset[i] = 0.0f;
    }
}

static void migrate_v1(const b2_settings_v1_t *old, b2_settings_t *settings)
{
    set_defaults(settings);
    settings->restore_relay_state = old->restore_relay_state;
    memcpy(settings->relay_state, old->relay_state, sizeof(settings->relay_state));
    settings->sms_auth_mode = old->sms_auth_mode == B2_SMS_AUTH_ALLOWLIST ? B2_SMS_AUTH_ALLOWLIST : B2_SMS_AUTH_ALLOW_ALL;
    settings->sms_allowlist_count = old->sms_allowlist_count <= B2_SETTINGS_SMS_ALLOWLIST_MAX ? old->sms_allowlist_count : 0;
    memcpy(settings->sms_allowlist, old->sms_allowlist, sizeof(settings->sms_allowlist));
    snprintf(settings->apn, sizeof(settings->apn), "%s", old->apn);
    snprintf(settings->mqtt_uri, sizeof(settings->mqtt_uri), "%s", old->mqtt_uri);
    settings->mqtt_enabled = old->mqtt_enabled;
    snprintf(settings->mqtt_username, sizeof(settings->mqtt_username), "%s", old->mqtt_username);
    snprintf(settings->mqtt_password, sizeof(settings->mqtt_password), "%s", old->mqtt_password);
    snprintf(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic), "%s", old->mqtt_base_topic);
    settings->wifi_enabled = old->wifi_enabled;
    snprintf(settings->wifi_ssid, sizeof(settings->wifi_ssid), "%s", old->wifi_ssid);
    snprintf(settings->wifi_password, sizeof(settings->wifi_password), "%s", old->wifi_password);
}

static void migrate_v2(const b2_settings_v2_t *old, b2_settings_t *settings)
{
    set_defaults(settings);
    settings->restore_relay_state = old->restore_relay_state;
    memcpy(settings->relay_state, old->relay_state, sizeof(settings->relay_state));
    settings->relay_interlock = old->relay_interlock;
    settings->relay_fail_safe_off = old->relay_fail_safe_off;
    settings->sms_auth_mode = old->sms_auth_mode;
    settings->sms_allowlist_count = old->sms_allowlist_count <= B2_SETTINGS_SMS_ALLOWLIST_MAX ? old->sms_allowlist_count : 0;
    memcpy(settings->sms_allowlist, old->sms_allowlist, sizeof(settings->sms_allowlist));
    settings->sms_rate_limit_seconds = old->sms_rate_limit_seconds;
    snprintf(settings->sms_shared_secret, sizeof(settings->sms_shared_secret), "%s", old->sms_shared_secret);
    snprintf(settings->apn, sizeof(settings->apn), "%s", old->apn);
    snprintf(settings->apn_username, sizeof(settings->apn_username), "%s", old->apn_username);
    snprintf(settings->apn_password, sizeof(settings->apn_password), "%s", old->apn_password);
    snprintf(settings->mqtt_uri, sizeof(settings->mqtt_uri), "%s", old->mqtt_uri);
    settings->mqtt_enabled = old->mqtt_enabled;
    settings->mqtt_allow_plaintext = old->mqtt_allow_plaintext;
    snprintf(settings->mqtt_username, sizeof(settings->mqtt_username), "%s", old->mqtt_username);
    snprintf(settings->mqtt_password, sizeof(settings->mqtt_password), "%s", old->mqtt_password);
    snprintf(settings->mqtt_ca_certificate, sizeof(settings->mqtt_ca_certificate), "%s", old->mqtt_ca_certificate);
    snprintf(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic), "%s", old->mqtt_base_topic);
    settings->wifi_enabled = old->wifi_enabled;
    snprintf(settings->wifi_ssid, sizeof(settings->wifi_ssid), "%s", old->wifi_ssid);
    snprintf(settings->wifi_password, sizeof(settings->wifi_password), "%s", old->wifi_password);
    snprintf(settings->http_auth_token, sizeof(settings->http_auth_token), "%s", old->http_auth_token);
    settings->http_auth_required = old->http_auth_required;
    settings->time_sync_enabled = old->time_sync_enabled;
    snprintf(settings->timezone, sizeof(settings->timezone), "%s", old->timezone);
    memcpy(settings->analog_gain, old->analog_gain, sizeof(settings->analog_gain));
    memcpy(settings->analog_offset, old->analog_offset, sizeof(settings->analog_offset));
    settings->rule_count = old->rule_count <= B2_SETTINGS_RULE_COUNT ? old->rule_count : 0;
    memcpy(settings->rules, old->rules, sizeof(settings->rules));
}

static void migrate_v3(const b2_settings_v3_t *old, b2_settings_t *settings)
{
    set_defaults(settings);
    settings->restore_relay_state = old->restore_relay_state;
    memcpy(settings->relay_state, old->relay_state, sizeof(settings->relay_state));
    settings->relay_interlock = old->relay_interlock;
    settings->relay_fail_safe_off = old->relay_fail_safe_off;
    settings->sms_auth_mode = old->sms_auth_mode;
    settings->sms_allowlist_count = old->sms_allowlist_count <= B2_SETTINGS_SMS_ALLOWLIST_MAX ? old->sms_allowlist_count : 0;
    memcpy(settings->sms_allowlist, old->sms_allowlist, sizeof(settings->sms_allowlist));
    settings->sms_rate_limit_seconds = old->sms_rate_limit_seconds;
    snprintf(settings->sms_shared_secret, sizeof(settings->sms_shared_secret), "%s", old->sms_shared_secret);
    snprintf(settings->apn, sizeof(settings->apn), "%s", old->apn);
    snprintf(settings->apn_username, sizeof(settings->apn_username), "%s", old->apn_username);
    snprintf(settings->apn_password, sizeof(settings->apn_password), "%s", old->apn_password);
    settings->apn_auth_type = old->apn_auth_type;
    snprintf(settings->mqtt_uri, sizeof(settings->mqtt_uri), "%s", old->mqtt_uri);
    settings->mqtt_enabled = old->mqtt_enabled;
    settings->mqtt_allow_plaintext = old->mqtt_allow_plaintext;
    snprintf(settings->mqtt_username, sizeof(settings->mqtt_username), "%s", old->mqtt_username);
    snprintf(settings->mqtt_password, sizeof(settings->mqtt_password), "%s", old->mqtt_password);
    snprintf(settings->mqtt_ca_certificate, sizeof(settings->mqtt_ca_certificate), "%s", old->mqtt_ca_certificate);
    snprintf(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic), "%s", old->mqtt_base_topic);
    settings->wifi_enabled = old->wifi_enabled;
    snprintf(settings->wifi_ssid, sizeof(settings->wifi_ssid), "%s", old->wifi_ssid);
    snprintf(settings->wifi_password, sizeof(settings->wifi_password), "%s", old->wifi_password);
    snprintf(settings->http_auth_token, sizeof(settings->http_auth_token), "%s", old->http_auth_token);
    settings->http_auth_required = old->http_auth_required;
    settings->time_sync_enabled = old->time_sync_enabled;
    snprintf(settings->timezone, sizeof(settings->timezone), "%s", old->timezone);
    memcpy(settings->analog_gain, old->analog_gain, sizeof(settings->analog_gain));
    memcpy(settings->analog_offset, old->analog_offset, sizeof(settings->analog_offset));
    settings->rule_count = old->rule_count <= B2_SETTINGS_RULE_COUNT ? old->rule_count : 0;
    memcpy(settings->rules, old->rules, sizeof(settings->rules));
}

static bool terminated(const char *value, size_t capacity)
{
    return strnlen(value, capacity) < capacity;
}

static esp_err_t validate(const b2_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG, "null settings");
    ESP_RETURN_ON_FALSE(settings->version == SETTINGS_VERSION, ESP_ERR_INVALID_VERSION, TAG, "unsupported settings version");
    ESP_RETURN_ON_FALSE(settings->sms_auth_mode <= B2_SMS_AUTH_ALLOW_ALL, ESP_ERR_INVALID_ARG, TAG, "invalid SMS auth mode");
    ESP_RETURN_ON_FALSE(settings->apn_auth_type <= B2_APN_AUTH_CHAP, ESP_ERR_INVALID_ARG, TAG, "invalid APN auth type");
    ESP_RETURN_ON_FALSE(settings->sms_allowlist_count <= B2_SETTINGS_SMS_ALLOWLIST_MAX, ESP_ERR_INVALID_ARG, TAG, "too many SMS numbers");
    ESP_RETURN_ON_FALSE(settings->rule_count <= B2_SETTINGS_RULE_COUNT, ESP_ERR_INVALID_ARG, TAG, "too many rules");
    ESP_RETURN_ON_FALSE(settings->sms_rate_limit_seconds <= 86400, ESP_ERR_INVALID_ARG, TAG, "invalid SMS rate limit");
    ESP_RETURN_ON_FALSE(terminated(settings->apn, sizeof(settings->apn)) &&
                        terminated(settings->apn_username, sizeof(settings->apn_username)) &&
                        terminated(settings->apn_password, sizeof(settings->apn_password)) &&
                        terminated(settings->mqtt_uri, sizeof(settings->mqtt_uri)) &&
                        terminated(settings->mqtt_username, sizeof(settings->mqtt_username)) &&
                        terminated(settings->mqtt_password, sizeof(settings->mqtt_password)) &&
                        terminated(settings->mqtt_ca_certificate, sizeof(settings->mqtt_ca_certificate)) &&
                        terminated(settings->mqtt_base_topic, sizeof(settings->mqtt_base_topic)) &&
                        terminated(settings->wifi_ssid, sizeof(settings->wifi_ssid)) &&
                        terminated(settings->wifi_password, sizeof(settings->wifi_password)) &&
                        terminated(settings->http_auth_token, sizeof(settings->http_auth_token)) &&
                        terminated(settings->sms_shared_secret, sizeof(settings->sms_shared_secret)) &&
                        terminated(settings->timezone, sizeof(settings->timezone)) &&
                        terminated(settings->sntp_server, sizeof(settings->sntp_server)),
                        ESP_ERR_INVALID_SIZE, TAG, "unterminated setting string");
    for (size_t i = 0; i < B2_SETTINGS_SMS_ALLOWLIST_MAX; ++i) {
        ESP_RETURN_ON_FALSE(terminated(settings->sms_allowlist[i], B2_SETTINGS_PHONE_MAX), ESP_ERR_INVALID_SIZE, TAG, "unterminated SMS number");
    }
    for (size_t i = 0; i < B2_ANALOG_CALIBRATION_COUNT; ++i) {
        ESP_RETURN_ON_FALSE(isfinite(settings->analog_gain[i]) && isfinite(settings->analog_offset[i]) && settings->analog_gain[i] > 0.0f,
                            ESP_ERR_INVALID_ARG, TAG, "invalid analog calibration");
    }
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
    if (err == ESP_OK && length == sizeof(*settings) && validate(settings) == ESP_OK) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        set_defaults(settings);
        return ESP_OK;
    }

    b2_settings_v3_t old_v3 = {0};
    size_t old_v3_length = sizeof(old_v3);
    esp_err_t old_v3_err = nvs_get_blob(handle, NVS_KEY, &old_v3, &old_v3_length);
    if (old_v3_err == ESP_OK && old_v3_length == sizeof(old_v3) && old_v3.version == 3) {
        nvs_close(handle);
        migrate_v3(&old_v3, settings);
        return b2_settings_save(settings);
    }

    b2_settings_v2_t old_v2 = {0};
    size_t old_v2_length = sizeof(old_v2);
    esp_err_t old_v2_err = nvs_get_blob(handle, NVS_KEY, &old_v2, &old_v2_length);
    if (old_v2_err == ESP_OK && old_v2_length == sizeof(old_v2) && old_v2.version == 2) {
        nvs_close(handle);
        migrate_v2(&old_v2, settings);
        return b2_settings_save(settings);
    }

    b2_settings_v1_t old = {0};
    size_t old_length = sizeof(old);
    esp_err_t old_err = nvs_get_blob(handle, NVS_KEY, &old, &old_length);
    nvs_close(handle);
    if (old_err == ESP_OK && old_length == sizeof(old) && old.version == 1) {
        migrate_v1(&old, settings);
        return b2_settings_save(settings);
    }
    set_defaults(settings);
    return ESP_ERR_INVALID_STATE;
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
