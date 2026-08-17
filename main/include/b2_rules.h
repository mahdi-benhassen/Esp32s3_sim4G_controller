#pragma once

#include "b2_settings.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef esp_err_t (*b2_rules_sms_callback_t)(const char *number, const char *message);
typedef esp_err_t (*b2_rules_mqtt_callback_t)(const char *event_name);

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_rules_init(const b2_settings_t *settings, b2_rules_sms_callback_t sms_callback,
                        b2_rules_mqtt_callback_t mqtt_callback);
void b2_rules_on_input(uint8_t channel, bool active);
void b2_rules_poll(void);
uint32_t b2_rules_fired_count(void);

#ifdef __cplusplus
}
#endif
