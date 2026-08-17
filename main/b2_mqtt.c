#include "b2_mqtt.h"

#include "b2_inputs.h"
#include "b2_relay.h"
#include "b2_settings.h"
#include "mqtt_client.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "b2_mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_connected;
static int s_last_message_id = -1;
static char s_uri[128];
static char s_base_topic[96];

static bool build_topic(char *out, size_t capacity, const char *suffix)
{
    if (strlcpy(out, s_base_topic, capacity) >= capacity) {
        return false;
    }
    size_t used = strlen(out);
    if (used + 1 >= capacity || strlcat(out, "/", capacity) >= capacity) {
        return false;
    }
    return strlcat(out, suffix, capacity) < capacity;
}

static bool topic_is(esp_mqtt_event_handle_t event, const char *suffix)
{
    char topic[128] = {0};
    if (!build_topic(topic, sizeof(topic), suffix)) {
        return false;
    }
    size_t length = strlen(topic);
    return event->topic_len == (int)length && memcmp(event->topic, topic, length) == 0;
}

static esp_err_t publish_discovery_config(const char *component, const char *object_id, const char *payload)
{
    ESP_RETURN_ON_FALSE(s_client != NULL && s_connected, ESP_ERR_INVALID_STATE, TAG, "MQTT is not connected");
    char topic[192] = {0};
    const int length = snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config", component, object_id);
    ESP_RETURN_ON_FALSE(length > 0 && length < (int)sizeof(topic), ESP_ERR_INVALID_SIZE, TAG, "discovery topic too large");
    const int message_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
    return message_id >= 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t publish_switch_discovery(uint8_t channel)
{
    char command_topic[128] = {0};
    char state_topic[128] = {0};
    char payload[768] = {0};
    const unsigned number = channel + 1U;
    ESP_RETURN_ON_FALSE(build_topic(command_topic, sizeof(command_topic), number == 1U ? "relay/1/set" : "relay/2/set"), ESP_ERR_INVALID_SIZE, TAG, "relay command topic too large");
    ESP_RETURN_ON_FALSE(build_topic(state_topic, sizeof(state_topic), "state"), ESP_ERR_INVALID_SIZE, TAG, "state topic too large");
    const int length = snprintf(payload, sizeof(payload),
                                 "{\"name\":\"B2 Relay %u\",\"unique_id\":\"b2_esp32s3_controller_relay%u\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ 'ON' if value_json.relay%u else 'OFF' }}\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"state_on\":\"ON\",\"state_off\":\"OFF\",\"device\":{\"identifiers\":[\"b2_esp32s3_controller\"],\"name\":\"B2 ESP32-S3 Controller\",\"manufacturer\":\"Independent ESP-IDF firmware\",\"model\":\"B2-compatible\"}}",
                                 number, number, command_topic, state_topic, number);
    ESP_RETURN_ON_FALSE(length > 0 && length < (int)sizeof(payload), ESP_ERR_INVALID_SIZE, TAG, "switch discovery payload too large");
    char object_id[64] = {0};
    snprintf(object_id, sizeof(object_id), "b2_esp32s3_controller_relay%u", number);
    return publish_discovery_config("switch", object_id, payload);
}

static esp_err_t publish_binary_sensor_discovery(uint8_t channel)
{
    char state_topic[128] = {0};
    char payload[704] = {0};
    const unsigned number = channel + 1U;
    ESP_RETURN_ON_FALSE(build_topic(state_topic, sizeof(state_topic), "state"), ESP_ERR_INVALID_SIZE, TAG, "state topic too large");
    const int length = snprintf(payload, sizeof(payload),
                                 "{\"name\":\"B2 Input %u\",\"unique_id\":\"b2_esp32s3_controller_input%u\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.input%u }}\",\"payload_on\":\"1\",\"payload_off\":\"0\",\"device_class\":\"connectivity\",\"device\":{\"identifiers\":[\"b2_esp32s3_controller\"],\"name\":\"B2 ESP32-S3 Controller\",\"manufacturer\":\"Independent ESP-IDF firmware\",\"model\":\"B2-compatible\"}}",
                                 number, number, state_topic, number);
    ESP_RETURN_ON_FALSE(length > 0 && length < (int)sizeof(payload), ESP_ERR_INVALID_SIZE, TAG, "input discovery payload too large");
    char object_id[64] = {0};
    snprintf(object_id, sizeof(object_id), "b2_esp32s3_controller_input%u", number);
    return publish_discovery_config("binary_sensor", object_id, payload);
}

static bool payload_is(esp_mqtt_event_handle_t event, const char *value)
{
    size_t length = strlen(value);
    return event->data_len == (int)length && strncasecmp(event->data, value, length) == 0;
}

static void process_relay_command(esp_mqtt_event_handle_t event, uint8_t channel)
{
    bool changed = false;
    if (payload_is(event, "ON")) {
        changed = b2_relay_set(channel, true) == ESP_OK;
    } else if (payload_is(event, "OFF")) {
        changed = b2_relay_set(channel, false) == ESP_OK;
    } else if (payload_is(event, "TOGGLE")) {
        changed = b2_relay_toggle(channel) == ESP_OK;
    }
    if (changed) {
        b2_mqtt_publish_state();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        char topic[128] = {0};
        if (build_topic(topic, sizeof(topic), "relay/1/set")) {
            esp_mqtt_client_subscribe(s_client, topic, 1);
        }
        if (build_topic(topic, sizeof(topic), "relay/2/set")) {
            esp_mqtt_client_subscribe(s_client, topic, 1);
        }
        b2_mqtt_publish_home_assistant_discovery();
        b2_mqtt_publish_state();
        ESP_LOGI(TAG, "MQTT connected to %s", s_uri);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected; client will retry");
        break;
    case MQTT_EVENT_DATA:
        if (topic_is(event, "relay/1/set")) {
            process_relay_command(event, 0);
        } else if (topic_is(event, "relay/2/set")) {
            process_relay_command(event, 1);
        }
        break;
    default:
        break;
    }
}

esp_err_t b2_mqtt_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    b2_settings_t settings;
    ESP_RETURN_ON_ERROR(b2_settings_load(&settings), TAG, "load MQTT settings");
    ESP_RETURN_ON_FALSE(settings.mqtt_enabled && settings.mqtt_uri[0] != '\0', ESP_ERR_NOT_FOUND, TAG, "MQTT disabled or URI missing");

    esp_mqtt_client_config_t config = {0};
    config.broker.address.uri = settings.mqtt_uri;
    if (settings.mqtt_username[0] != '\0') {
        config.credentials.username = settings.mqtt_username;
    }
    if (settings.mqtt_password[0] != '\0') {
        config.credentials.authentication.password = settings.mqtt_password;
    }
    s_client = esp_mqtt_client_init(&config);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_NO_MEM, TAG, "create MQTT client");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL), TAG, "register MQTT events");
    snprintf(s_uri, sizeof(s_uri), "%s", settings.mqtt_uri);
    snprintf(s_base_topic, sizeof(s_base_topic), "%s", settings.mqtt_base_topic[0] != '\0' ? settings.mqtt_base_topic : "b2/controller");
    s_started = true;
    return esp_mqtt_client_start(s_client);
}

esp_err_t b2_mqtt_stop(void)
{
    if (!s_client) {
        return ESP_OK;
    }
    s_started = false;
    s_connected = false;
    esp_err_t err = esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    return err;
}

esp_err_t b2_mqtt_get_status(b2_mqtt_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "null MQTT status");
    memset(status, 0, sizeof(*status));
    status->started = s_started;
    status->connected = s_connected;
    status->last_message_id = s_last_message_id;
    snprintf(status->uri, sizeof(status->uri), "%s", s_uri);
    snprintf(status->base_topic, sizeof(status->base_topic), "%s", s_base_topic);
    return ESP_OK;
}

esp_err_t b2_mqtt_publish_home_assistant_discovery(void)
{
    ESP_RETURN_ON_ERROR(publish_switch_discovery(0), TAG, "publish relay 1 discovery");
    ESP_RETURN_ON_ERROR(publish_switch_discovery(1), TAG, "publish relay 2 discovery");
    ESP_RETURN_ON_ERROR(publish_binary_sensor_discovery(0), TAG, "publish input 1 discovery");
    return publish_binary_sensor_discovery(1);
}

esp_err_t b2_mqtt_publish_state(void)
{
    ESP_RETURN_ON_FALSE(s_client != NULL && s_connected, ESP_ERR_INVALID_STATE, TAG, "MQTT is not connected");
    char topic[128] = {0};
    char payload[160] = {0};
    bool relay1 = false;
    bool relay2 = false;
    b2_relay_get(0, &relay1);
    b2_relay_get(1, &relay2);
    int input1 = b2_input_is_active(0) ? 1 : 0;
    int input2 = b2_input_is_active(1) ? 1 : 0;
    int length = snprintf(payload, sizeof(payload), "{\"relay1\":%s,\"relay2\":%s,\"input1\":%d,\"input2\":%d}",
                          relay1 ? "true" : "false", relay2 ? "true" : "false", input1, input2);
    ESP_RETURN_ON_FALSE(length > 0 && length < (int)sizeof(payload), ESP_ERR_INVALID_SIZE, TAG, "MQTT state payload too large");
    ESP_RETURN_ON_FALSE(build_topic(topic, sizeof(topic), "state"), ESP_ERR_INVALID_SIZE, TAG, "MQTT topic too large");
    s_last_message_id = esp_mqtt_client_publish(s_client, topic, payload, length, 1, 1);
    return s_last_message_id >= 0 ? ESP_OK : ESP_FAIL;
}
