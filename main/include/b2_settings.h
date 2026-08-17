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
#define B2_SETTINGS_APN_USERNAME_MAX 65
#define B2_SETTINGS_APN_PASSWORD_MAX 65
#define B2_SETTINGS_MQTT_URI_MAX 128
#define B2_SETTINGS_MQTT_USERNAME_MAX 65
#define B2_SETTINGS_MQTT_PASSWORD_MAX 65
#define B2_SETTINGS_MQTT_TOPIC_MAX 96
#define B2_SETTINGS_MQTT_CA_MAX 2048
#define B2_SETTINGS_WIFI_SSID_MAX 33
#define B2_SETTINGS_WIFI_PASSWORD_MAX 65
#define B2_SETTINGS_HTTP_TOKEN_MAX 65
#define B2_SETTINGS_SMS_SECRET_MAX 33
#define B2_SETTINGS_TIMEZONE_MAX 40
#define B2_SETTINGS_RULE_COUNT 8
#define B2_SETTINGS_RULE_SMS_MAX 24
#define B2_ANALOG_CALIBRATION_COUNT 4

typedef enum {
    B2_SMS_AUTH_ALLOWLIST = 0,
    B2_SMS_AUTH_ALLOW_ALL = 1,
} b2_sms_auth_mode_t;

typedef enum {
    B2_APN_AUTH_NONE = 0,
    B2_APN_AUTH_PAP = 1,
    B2_APN_AUTH_CHAP = 2,
} b2_apn_auth_type_t;

typedef enum {
    B2_RULE_DISABLED = 0,
    B2_RULE_INPUT_ACTIVE = 1,
    B2_RULE_ADC_ABOVE = 2,
    B2_RULE_ADC_BELOW = 3,
    B2_RULE_CSQ_BELOW = 4,
} b2_rule_condition_t;

typedef enum {
    B2_RULE_ACTION_NONE = 0,
    B2_RULE_ACTION_RELAY_SET = 1,
    B2_RULE_ACTION_RELAY_TOGGLE = 2,
    B2_RULE_ACTION_SMS = 3,
    B2_RULE_ACTION_MQTT_EVENT = 4,
} b2_rule_action_t;

typedef struct {
    bool enabled;
    uint8_t condition;
    uint8_t source;
    uint8_t action;
    uint8_t target;
    bool action_state;
    float threshold;
    uint32_t duration_ms;
    char sms_number[B2_SETTINGS_RULE_SMS_MAX];
} b2_rule_t;

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
} b2_settings_t;

esp_err_t b2_settings_load(b2_settings_t *settings);
esp_err_t b2_settings_save(const b2_settings_t *settings);
esp_err_t b2_settings_reset(void);

#ifdef __cplusplus
}
#endif
