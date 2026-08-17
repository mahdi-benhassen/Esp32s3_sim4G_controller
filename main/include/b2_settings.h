#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define B2_SETTINGS_SMS_ALLOWLIST_MAX 4
#define B2_SETTINGS_PHONE_MAX 24
#define B2_SETTINGS_APN_MAX 64
#define B2_SETTINGS_MQTT_URI_MAX 128
#define B2_SETTINGS_MQTT_USERNAME_MAX 65
#define B2_SETTINGS_MQTT_PASSWORD_MAX 65
#define B2_SETTINGS_MQTT_TOPIC_MAX 96
#define B2_SETTINGS_WIFI_SSID_MAX 33
#define B2_SETTINGS_WIFI_PASSWORD_MAX 65

typedef enum {
    B2_SMS_AUTH_ALLOW_ALL = 0,
    B2_SMS_AUTH_ALLOWLIST = 1,
} b2_sms_auth_mode_t;

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
} b2_settings_t;

esp_err_t b2_settings_load(b2_settings_t *settings);
esp_err_t b2_settings_save(const b2_settings_t *settings);
esp_err_t b2_settings_reset(void);

#ifdef __cplusplus
}
#endif
