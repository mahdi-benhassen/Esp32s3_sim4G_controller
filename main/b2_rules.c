#include "b2_rules.h"

#include "b2_adc.h"
#include "b2_config.h"
#include "b2_core.h"
#include "b2_eventlog.h"
#include "b2_modem.h"
#include "b2_relay.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_rules";
static b2_settings_t s_settings;
static b2_rules_sms_callback_t s_sms_callback;
static b2_rules_mqtt_callback_t s_mqtt_callback;
static bool s_input_state[B2_DRY_INPUT_COUNT];
static int64_t s_input_since[B2_DRY_INPUT_COUNT];
static bool s_fired[B2_SETTINGS_RULE_COUNT];
static uint32_t s_fired_count;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

esp_err_t b2_rules_validate_definition(const b2_rule_t *rule)
{
    if (rule == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const b2_core_rule_t core = {
        .enabled = rule->enabled,
        .condition = rule->condition,
        .source = rule->source,
        .action = rule->action,
        .target = rule->target,
        .action_state = rule->action_state,
        .threshold = rule->threshold,
        .duration_ms = rule->duration_ms,
        .sms_number = rule->sms_number,
    };
    return b2_core_rule_is_valid(&core, B2_RELAY_COUNT, B2_DRY_INPUT_COUNT,
                                 B2_ANALOG_CALIBRATION_COUNT) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static bool condition_ready(const b2_rule_t *rule, int64_t now_us)
{
    if (rule->condition == B2_RULE_INPUT_ACTIVE) {
        if (rule->source >= B2_DRY_INPUT_COUNT || !s_input_state[rule->source]) {
            return false;
        }
        const int64_t since = s_input_since[rule->source];
        return since > 0 && now_us - since >= (int64_t)rule->duration_ms * 1000LL;
    }
    if (rule->condition == B2_RULE_ADC_ABOVE || rule->condition == B2_RULE_ADC_BELOW) {
        if (rule->source >= B2_ANALOG_CALIBRATION_COUNT) {
            return false;
        }
        float value = 0.0f;
        if (b2_adc_read_voltage(rule->source, &value) != ESP_OK) {
            return false;
        }
        return rule->condition == B2_RULE_ADC_ABOVE ? value >= rule->threshold : value <= rule->threshold;
    }
    if (rule->condition == B2_RULE_CSQ_BELOW) {
        b2_modem_status_t modem = {0};
        if (b2_modem_get_status(&modem) != ESP_OK) {
            return false;
        }
        return modem.signal_quality >= 0 && modem.signal_quality <= (int)rule->threshold;
    }
    return false;
}

static void fire_rule(uint8_t index, const b2_rule_t *rule)
{
    char description[64] = {0};
    snprintf(description, sizeof(description), "rule%u action%u", index + 1U, rule->action);
    b2_event_log_append(B2_EVENT_RULE, index, rule->target, description);
    bool action_ok = false;
    if (rule->action == B2_RULE_ACTION_RELAY_SET && rule->target < B2_RELAY_COUNT) {
        action_ok = b2_relay_set(rule->target, rule->action_state) == ESP_OK;
    } else if (rule->action == B2_RULE_ACTION_RELAY_TOGGLE && rule->target < B2_RELAY_COUNT) {
        action_ok = b2_relay_toggle(rule->target) == ESP_OK;
    } else if (rule->action == B2_RULE_ACTION_SMS && s_sms_callback != NULL) {
        action_ok = s_sms_callback(rule->sms_number, description) == ESP_OK;
    } else if (rule->action == B2_RULE_ACTION_MQTT_EVENT && s_mqtt_callback != NULL) {
        action_ok = s_mqtt_callback(description) == ESP_OK;
    }
    ESP_LOGI(TAG, "rule %u fired action=%u result=%s", index + 1U, rule->action, action_ok ? "ok" : "failed");
    s_fired_count++;
}

esp_err_t b2_rules_reload(const b2_settings_t *settings)
{
    if (settings == NULL || settings->rule_count > B2_SETTINGS_RULE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < settings->rule_count; ++i) {
        if (b2_rules_validate_definition(&settings->rules[i]) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_settings = *settings;
    memset(s_fired, 0, sizeof(s_fired));
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "rules reloaded (%u slots)", settings->rule_count);
    return ESP_OK;
}

esp_err_t b2_rules_init(const b2_settings_t *settings, b2_rules_sms_callback_t sms_callback,
                        b2_rules_mqtt_callback_t mqtt_callback)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_sms_callback = sms_callback;
    s_mqtt_callback = mqtt_callback;
    memset(s_input_state, 0, sizeof(s_input_state));
    memset(s_input_since, 0, sizeof(s_input_since));
    memset(s_fired, 0, sizeof(s_fired));
    s_fired_count = 0;
    xSemaphoreGive(s_mutex);
    ESP_RETURN_ON_ERROR(b2_rules_reload(settings), TAG, "load rules");
    return b2_event_log_append(B2_EVENT_BOOT, 0, 0, "rules-ready");
}

void b2_rules_on_input(uint8_t channel, bool active)
{
    if (channel >= B2_DRY_INPUT_COUNT || s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_input_state[channel] = active;
    s_input_since[channel] = active ? esp_timer_get_time() : 0;
    for (size_t i = 0; i < s_settings.rule_count; ++i) {
        if (s_settings.rules[i].condition == B2_RULE_INPUT_ACTIVE && s_settings.rules[i].source == channel && !active) {
            s_fired[i] = false;
        }
    }
    xSemaphoreGive(s_mutex);
}

void b2_rules_poll(void)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < s_settings.rule_count; ++i) {
        const b2_rule_t *rule = &s_settings.rules[i];
        if (!rule->enabled || rule->condition == B2_RULE_DISABLED) {
            continue;
        }
        const bool ready = condition_ready(rule, now);
        if (!ready) {
            s_fired[i] = false;
            continue;
        }
        if (!s_fired[i]) {
            fire_rule((uint8_t)i, rule);
            s_fired[i] = true;
        }
    }
    xSemaphoreGive(s_mutex);
}

uint32_t b2_rules_fired_count(void)
{
    if (s_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t count = s_fired_count;
    xSemaphoreGive(s_mutex);
    return count;
}
