#include "b2_core.h"

#include "b2_settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static size_t b2_core_bounded_length(const char *value, size_t limit)
{
    if (value == NULL) {
        return 0U;
    }
    size_t length = 0U;
    while (length < limit && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool b2_core_rule_is_valid(const b2_core_rule_t *rule, uint8_t relay_count,
                           uint8_t input_count, uint8_t analog_count)
{
    if (rule == NULL) {
        return false;
    }
    if (!rule->enabled || rule->condition == B2_RULE_DISABLED) {
        return true;
    }
    if (rule->duration_ms > 86400000U || !isfinite(rule->threshold)) {
        return false;
    }
    if (rule->condition == B2_RULE_INPUT_ACTIVE && rule->source >= input_count) {
        return false;
    }
    if ((rule->condition == B2_RULE_ADC_ABOVE || rule->condition == B2_RULE_ADC_BELOW) &&
        rule->source >= analog_count) {
        return false;
    }
    if (rule->condition != B2_RULE_INPUT_ACTIVE && rule->condition != B2_RULE_ADC_ABOVE &&
        rule->condition != B2_RULE_ADC_BELOW && rule->condition != B2_RULE_CSQ_BELOW) {
        return false;
    }
    if (rule->action == B2_RULE_ACTION_RELAY_SET || rule->action == B2_RULE_ACTION_RELAY_TOGGLE) {
        return rule->target < relay_count;
    }
    if (rule->action == B2_RULE_ACTION_SMS) {
        return rule->sms_number != NULL &&
               b2_core_bounded_length(rule->sms_number, B2_SETTINGS_RULE_SMS_MAX) < B2_SETTINGS_RULE_SMS_MAX;
    }
    return rule->action == B2_RULE_ACTION_MQTT_EVENT;
}

bool b2_core_token_equal(const char *actual, const char *expected)
{
    if (actual == NULL || expected == NULL) {
        return false;
    }
    const size_t actual_len = strlen(actual);
    const size_t expected_len = strlen(expected);
    const size_t max_len = actual_len > expected_len ? actual_len : expected_len;
    unsigned diff = (unsigned)(actual_len ^ expected_len);
    for (size_t i = 0; i < max_len; ++i) {
        const unsigned char a = i < actual_len ? (unsigned char)actual[i] : 0;
        const unsigned char b = i < expected_len ? (unsigned char)expected[i] : 0;
        diff |= (unsigned)(a ^ b);
    }
    return diff == 0;
}

int b2_core_extract_sms_command(const char *shared_secret, const char *message,
                                char *command, size_t command_size)
{
    if (message == NULL || command == NULL || command_size <= 1U) {
        return -1;
    }
    const char *body = message;
    if (shared_secret != NULL && shared_secret[0] != '\0') {
        const char prefix[] = "TOKEN:";
        const size_t prefix_len = sizeof(prefix) - 1U;
        if (strncmp(body, prefix, prefix_len) != 0) {
            return -2;
        }
        const char *separator = strchr(body + prefix_len, ' ');
        if (separator == NULL) {
            return -2;
        }
        char token[B2_SETTINGS_SMS_SECRET_MAX] = {0};
        const size_t token_len = (size_t)(separator - (body + prefix_len));
        if (token_len == 0U || token_len >= sizeof(token)) {
            return -2;
        }
        memcpy(token, body + prefix_len, token_len);
        if (!b2_core_token_equal(token, shared_secret)) {
            return -2;
        }
        body = separator + 1;
    }
    const size_t body_len = strlen(body);
    if (body_len >= command_size) {
        return -3;
    }
    memcpy(command, body, body_len + 1U);
    return 0;
}

bool b2_core_settings_version_supported(uint32_t version, uint32_t current_version)
{
    return version >= 1U && version <= current_version;
}

void b2_core_event_ring_reset(b2_core_event_ring_t *ring)
{
    if (ring != NULL) {
        memset(ring, 0, sizeof(*ring));
    }
}

esp_err_t b2_core_event_ring_append(b2_core_event_ring_t *ring, int64_t timestamp_us,
                                    uint8_t type, uint8_t source, int32_t value,
                                    const char *text, uint32_t *sequence)
{
    if (ring == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b2_event_t *event = &ring->entries[ring->next];
    memset(event, 0, sizeof(*event));
    event->sequence = ring->count > 0U
                          ? ring->entries[(ring->next + B2_EVENT_LOG_CAPACITY - 1U) % B2_EVENT_LOG_CAPACITY].sequence + 1U
                          : 1U;
    event->timestamp_us = timestamp_us;
    event->type = type;
    event->source = source;
    event->value = value;
    if (text != NULL) {
        snprintf(event->text, sizeof(event->text), "%s", text);
    }
    ring->next = (ring->next + 1U) % B2_EVENT_LOG_CAPACITY;
    if (ring->count < B2_EVENT_LOG_CAPACITY) {
        ring->count++;
    }
    if (sequence != NULL) {
        *sequence = event->sequence;
    }
    return ESP_OK;
}

esp_err_t b2_core_event_ring_get_newest(const b2_core_event_ring_t *ring,
                                        uint8_t index, b2_event_t *event)
{
    if (ring == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index >= ring->count) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint32_t newest = (ring->next + B2_EVENT_LOG_CAPACITY - 1U) % B2_EVENT_LOG_CAPACITY;
    const uint32_t position = (newest + B2_EVENT_LOG_CAPACITY - index) % B2_EVENT_LOG_CAPACITY;
    *event = ring->entries[position];
    return ESP_OK;
}
