#include "b2_security.h"

#include "esp_check.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_security";
#define SMS_TRACK_SLOTS 4
#define SMS_REPLAY_WINDOW_MS (5U * 60U * 1000U)

typedef struct {
    bool used;
    char sender[B2_SETTINGS_PHONE_MAX];
    uint32_t last_ms;
    uint32_t message_hash;
} sms_track_t;

static sms_track_t s_sms_track[SMS_TRACK_SLOTS];

static uint32_t fnv1a(const char *text)
{
    uint32_t hash = 2166136261U;
    if (text == NULL) {
        return hash;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
        hash ^= *p;
        hash *= 16777619U;
    }
    return hash;
}

esp_err_t b2_security_init_nvs(void)
{
    const esp_partition_t *keys = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                              ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
                                                              NULL);
    ESP_RETURN_ON_FALSE(keys != NULL, ESP_ERR_NOT_FOUND, TAG, "nvs_keys partition is required");
    nvs_sec_cfg_t cfg = {0};
    esp_err_t err = nvs_flash_read_security_cfg(keys, &cfg);
    if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        err = nvs_flash_generate_keys(keys, &cfg);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "load or generate NVS encryption keys");
    return nvs_flash_secure_init(&cfg);
}

bool b2_security_token_equal(const char *actual, const char *expected)
{
    if (actual == NULL || expected == NULL) {
        return false;
    }
    const size_t actual_len = strlen(actual);
    const size_t expected_len = strlen(expected);
    size_t max_len = actual_len > expected_len ? actual_len : expected_len;
    unsigned diff = (unsigned)(actual_len ^ expected_len);
    for (size_t i = 0; i < max_len; ++i) {
        const unsigned char a = i < actual_len ? (unsigned char)actual[i] : 0;
        const unsigned char b = i < expected_len ? (unsigned char)expected[i] : 0;
        diff |= (unsigned)(a ^ b);
    }
    return diff == 0;
}

static int find_slot(const char *sender)
{
    int free_slot = -1;
    for (int i = 0; i < SMS_TRACK_SLOTS; ++i) {
        if (s_sms_track[i].used && strcmp(s_sms_track[i].sender, sender) == 0) {
            return i;
        }
        if (!s_sms_track[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    return free_slot >= 0 ? free_slot : 0;
}

bool b2_security_sms_accept(const b2_settings_t *settings, const char *sender, const char *message,
                            uint32_t now_ms)
{
    if (settings == NULL || sender == NULL || message == NULL || sender[0] == '\0' || message[0] == '\0') {
        return false;
    }
    if (settings->sms_auth_mode != B2_SMS_AUTH_ALLOW_ALL) {
        bool allowed = false;
        for (size_t i = 0; i < settings->sms_allowlist_count; ++i) {
            if (strcmp(sender, settings->sms_allowlist[i]) == 0) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            return false;
        }
    }

    const int slot = find_slot(sender);
    sms_track_t *track = &s_sms_track[slot];
    const uint32_t hash = fnv1a(message);
    if (track->used) {
        const uint32_t elapsed = now_ms - track->last_ms;
        if (settings->sms_rate_limit_seconds > 0 && elapsed < settings->sms_rate_limit_seconds * 1000U) {
            return false;
        }
        if (track->message_hash == hash && elapsed < SMS_REPLAY_WINDOW_MS) {
            return false;
        }
    }
    track->used = true;
    snprintf(track->sender, sizeof(track->sender), "%s", sender);
    track->last_ms = now_ms;
    track->message_hash = hash;
    return true;
}

esp_err_t b2_security_sms_command(const b2_settings_t *settings, const char *message,
                                  char *command, size_t command_size)
{
    ESP_RETURN_ON_FALSE(settings != NULL && message != NULL && command != NULL && command_size > 1,
                        ESP_ERR_INVALID_ARG, TAG, "invalid SMS command arguments");
    const char *body = message;
    if (settings->sms_shared_secret[0] != '\0') {
        const char prefix[] = "TOKEN:";
        const size_t prefix_len = sizeof(prefix) - 1U;
        ESP_RETURN_ON_FALSE(strncmp(body, prefix, prefix_len) == 0, ESP_ERR_INVALID_CRC, TAG, "SMS token missing");
        const char *separator = strchr(body + prefix_len, ' ');
        ESP_RETURN_ON_FALSE(separator != NULL, ESP_ERR_INVALID_CRC, TAG, "SMS token separator missing");
        char token[B2_SETTINGS_SMS_SECRET_MAX] = {0};
        size_t token_len = (size_t)(separator - (body + prefix_len));
        ESP_RETURN_ON_FALSE(token_len > 0 && token_len < sizeof(token), ESP_ERR_INVALID_CRC, TAG, "SMS token size invalid");
        memcpy(token, body + prefix_len, token_len);
        ESP_RETURN_ON_FALSE(b2_security_token_equal(token, settings->sms_shared_secret), ESP_ERR_INVALID_CRC, TAG, "SMS token rejected");
        body = separator + 1;
    }
    ESP_RETURN_ON_FALSE(strlcpy(command, body, command_size) < command_size, ESP_ERR_INVALID_SIZE, TAG, "SMS command too long");
    return ESP_OK;
}

void b2_security_mask_secret(const char *secret, char *masked, size_t masked_size)
{
    if (masked == NULL || masked_size == 0) {
        return;
    }
    if (secret == NULL || secret[0] == '\0') {
        snprintf(masked, masked_size, "<empty>");
        return;
    }
    const size_t length = strlen(secret);
    if (length < 3) {
        snprintf(masked, masked_size, "***");
        return;
    }
    snprintf(masked, masked_size, "%c***%c", secret[0], secret[length - 1]);
}
