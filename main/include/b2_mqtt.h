#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool connected;
    int last_message_id;
    char uri[128];
    char base_topic[96];
} b2_mqtt_status_t;

/** Start the configured MQTT client. Returns ESP_ERR_NOT_FOUND when disabled. */
esp_err_t b2_mqtt_start(void);
esp_err_t b2_mqtt_stop(void);
esp_err_t b2_mqtt_get_status(b2_mqtt_status_t *status);
esp_err_t b2_mqtt_publish_state(void);
esp_err_t b2_mqtt_publish_event(const char *event_name);
esp_err_t b2_mqtt_publish_home_assistant_discovery(void);

#ifdef __cplusplus
}
#endif
