#include "b2_http.h"

#include "b2_eventlog.h"
#include "b2_inputs.h"
#include "b2_modem.h"
#include "b2_mqtt.h"
#include "b2_relay.h"
#include "b2_rules.h"
#include "b2_security.h"
#include "b2_settings.h"
#include "b2_storage.h"
#include "b2_tls_credentials.h"
#include "b2_wifi.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "b2_http";
static httpd_handle_t s_server;
static bool s_started;
static bool s_tls;
static char s_server_certificate[B2_TLS_CREDENTIAL_MAX];
static char s_server_private_key[B2_TLS_CREDENTIAL_MAX];
static int64_t s_last_control_us;

static esp_err_t send_text(httpd_req_t *request, const char *content_type, const char *body)
{
    ESP_RETURN_ON_FALSE(request != NULL && body != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid HTTP response");
    httpd_resp_set_type(request, content_type);
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = send_text(request, "application/json", body);
    cJSON_free(body);
    return err;
}

static bool bearer_authorized(httpd_req_t *request)
{
    b2_settings_t settings = {0};
    if (b2_settings_load(&settings) != ESP_OK || settings.http_auth_token[0] == '\0') {
        return false;
    }
    size_t length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (length == 0 || length >= 128) {
        return false;
    }
    char header[128] = {0};
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) != ESP_OK) {
        return false;
    }
    const char prefix[] = "Bearer ";
    return strncmp(header, prefix, sizeof(prefix) - 1U) == 0 &&
           b2_security_token_equal(header + sizeof(prefix) - 1U, settings.http_auth_token);
}

static esp_err_t unauthorized(httpd_req_t *request)
{
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
    return send_text(request, "application/json", "{\"error\":\"authentication_required\"}\n");
}

static esp_err_t require_read_auth(httpd_req_t *request)
{
    if (!s_tls || !bearer_authorized(request)) {
        return unauthorized(request);
    }
    return ESP_OK;
}

static bool control_rate_allowed(void)
{
    const int64_t now = esp_timer_get_time();
    if (s_last_control_us != 0 && now - s_last_control_us < 10000000LL) {
        return false;
    }
    s_last_control_us = now;
    return true;
}

static esp_err_t require_control_auth(httpd_req_t *request)
{
    esp_err_t err = require_read_auth(request);
    if (err != ESP_OK) {
        return err;
    }
    if (!control_rate_allowed()) {
        httpd_resp_set_status(request, "429 Too Many Requests");
        return send_text(request, "application/json", "{\"error\":\"rate_limited\"}\n");
    }
    return ESP_OK;
}

static esp_err_t receive_body(httpd_req_t *request, char *body, size_t capacity)
{
    ESP_RETURN_ON_FALSE(request != NULL && body != NULL && capacity > 1U, ESP_ERR_INVALID_ARG, TAG, "invalid body buffer");
    if (request->content_len == 0 || request->content_len >= capacity) {
        httpd_resp_set_status(request, request->content_len >= capacity ? "413 Payload Too Large" : "400 Bad Request");
        return send_text(request, "application/json", request->content_len >= capacity ?
                         "{\"error\":\"payload_too_large\"}\n" : "{\"error\":\"empty_body\"}\n");
    }
    size_t received = 0;
    while (received < request->content_len) {
        int chunk = httpd_req_recv(request, body + received, request->content_len - received);
        if (chunk <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)chunk;
    }
    body[received] = '\0';
    return ESP_OK;
}

static esp_err_t health_handler(httpd_req_t *request)
{
    return send_text(request, "text/plain", "ok\n");
}

static esp_err_t capabilities_handler(httpd_req_t *request)
{
    static const char capabilities[] =
        "{\"device\":\"kincony-b2-compatible\",\"target\":\"esp32s3\","
        "\"api\":\"authenticated\",\"features\":[\"relay\",\"dry-input\",\"analog\","
        "\"onewire\",\"rtc\",\"oled\",\"sd\",\"rs485-modbus\",\"wifi\","
        "\"mqtt\",\"homeassistant-discovery\",\"sim7600\",\"gnss\",\"rules\",\"rules-crud\",\"time-sync\",\"ota\","
        "\"https\",\"authenticated-relay-write\",\"remote-diagnostics\",\"event-log\"]}\n";
    return send_text(request, "application/json", capabilities);
}

static esp_err_t relay_write_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_control_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    const char *uri = request->uri;
    size_t uri_len = strlen(uri);
    ESP_RETURN_ON_FALSE(uri_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid relay URI");
    uint8_t channel = uri[uri_len - 1U] == '1' ? 0 : 1;
    char body[32] = {0};
    if (receive_body(request, body, sizeof(body)) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t err;
    if (strncasecmp(body, "ON", 2) == 0) {
        err = b2_relay_set(channel, true);
    } else if (strncasecmp(body, "OFF", 3) == 0) {
        err = b2_relay_set(channel, false);
    } else if (strncasecmp(body, "TOGGLE", 6) == 0) {
        err = b2_relay_toggle(channel);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return send_text(request, "application/json", "{\"error\":\"invalid_relay_command\"}\n");
    }
    return send_text(request, "application/json", "{\"ok\":true}\n");
}

static cJSON *rule_to_json(const b2_rule_t *rule, uint8_t index)
{
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(item, "slot", index + 1U);
    cJSON_AddBoolToObject(item, "enabled", rule->enabled);
    cJSON_AddNumberToObject(item, "condition", rule->condition);
    cJSON_AddNumberToObject(item, "source", rule->source);
    cJSON_AddNumberToObject(item, "action", rule->action);
    cJSON_AddNumberToObject(item, "target", rule->target);
    cJSON_AddBoolToObject(item, "action_state", rule->action_state);
    cJSON_AddNumberToObject(item, "threshold", rule->threshold);
    cJSON_AddNumberToObject(item, "duration_ms", rule->duration_ms);
    cJSON_AddStringToObject(item, "sms_number", rule->sms_number);
    return item;
}

static bool json_uint(const cJSON *object, const char *name, unsigned max, unsigned *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)max || floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    *value = (unsigned)item->valuedouble;
    return true;
}

static bool json_float(const cJSON *object, const char *name, float *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *value = (float)item->valuedouble;
    return isfinite(*value);
}

static bool json_bool_or_default(const cJSON *object, const char *name, bool default_value, bool *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == NULL) {
        *value = default_value;
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item);
    return true;
}

static bool json_rule_to_native(const cJSON *object, b2_rule_t *rule)
{
    if (!cJSON_IsObject(object) || rule == NULL) {
        return false;
    }
    memset(rule, 0, sizeof(*rule));
    unsigned value = 0;
    if (!json_bool_or_default(object, "enabled", false, &rule->enabled) ||
        !json_uint(object, "condition", B2_RULE_CSQ_BELOW, &value)) {
        return false;
    }
    rule->condition = (uint8_t)value;
    if (rule->condition == B2_RULE_DISABLED) {
        rule->enabled = false;
        return b2_rules_validate_definition(rule) == ESP_OK;
    }
    if (!json_uint(object, "source", UINT8_MAX, &value)) {
        return false;
    }
    rule->source = (uint8_t)value;
    if (!json_uint(object, "action", B2_RULE_ACTION_MQTT_EVENT, &value)) {
        return false;
    }
    rule->action = (uint8_t)value;
    if (!json_uint(object, "target", UINT8_MAX, &value)) {
        return false;
    }
    rule->target = (uint8_t)value;
    if (!json_bool_or_default(object, "action_state", false, &rule->action_state) ||
        !json_float(object, "threshold", &rule->threshold) ||
        !json_uint(object, "duration_ms", 86400000U, &value)) {
        return false;
    }
    rule->duration_ms = value;
    const cJSON *sms = cJSON_GetObjectItemCaseSensitive(object, "sms_number");
    if (sms != NULL) {
        if (!cJSON_IsString(sms) || sms->valuestring == NULL || strlen(sms->valuestring) >= sizeof(rule->sms_number)) {
            return false;
        }
        snprintf(rule->sms_number, sizeof(rule->sms_number), "%s", sms->valuestring);
    }
    return b2_rules_validate_definition(rule) == ESP_OK;
}

static uint8_t recompute_rule_count(const b2_settings_t *settings)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < B2_SETTINGS_RULE_COUNT; ++i) {
        if (settings->rules[i].enabled && settings->rules[i].condition != B2_RULE_DISABLED) {
            count = i + 1U;
        }
    }
    return count;
}

static esp_err_t save_rules(b2_settings_t *settings)
{
    settings->rule_count = recompute_rule_count(settings);
    esp_err_t err = b2_settings_save(settings);
    if (err != ESP_OK) {
        return err;
    }
    return b2_rules_reload(settings);
}

static esp_err_t rules_collection_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_read_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    b2_settings_t settings = {0};
    ESP_RETURN_ON_ERROR(b2_settings_load(&settings), TAG, "load rules");
    cJSON *root = cJSON_CreateObject();
    cJSON *rules = cJSON_CreateArray();
    if (root == NULL || rules == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(rules);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddNumberToObject(root, "count", settings.rule_count);
    cJSON_AddItemToObject(root, "rules", rules);
    for (uint8_t i = 0; i < B2_SETTINGS_RULE_COUNT; ++i) {
        cJSON *item = rule_to_json(&settings.rules[i], i);
        if (item == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToArray(rules, item);
    }
    esp_err_t err = send_json(request, root);
    cJSON_Delete(root);
    return err;
}

static bool parse_rule_slot(const char *uri, uint8_t *slot)
{
    if (uri == NULL || slot == NULL || !httpd_uri_match_wildcard("/api/v1/rules/*", uri, strlen(uri))) {
        return false;
    }
    const char *last = strrchr(uri, '/');
    if (last == NULL || last[1] == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(last + 1, &end, 10);
    if (end == last + 1 || *end != '\0' || parsed < 1U || parsed > B2_SETTINGS_RULE_COUNT) {
        return false;
    }
    *slot = (uint8_t)(parsed - 1U);
    return true;
}

static esp_err_t rule_item_get_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_read_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    uint8_t slot = 0;
    if (!parse_rule_slot(request->uri, &slot)) {
        httpd_resp_set_status(request, "404 Not Found");
        return send_text(request, "application/json", "{\"error\":\"invalid_rule_slot\"}\n");
    }
    b2_settings_t settings = {0};
    ESP_RETURN_ON_ERROR(b2_settings_load(&settings), TAG, "load rule");
    cJSON *item = rule_to_json(&settings.rules[slot], slot);
    if (item == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = send_json(request, item);
    cJSON_Delete(item);
    return err;
}

static esp_err_t rule_item_put_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_control_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    uint8_t slot = 0;
    if (!parse_rule_slot(request->uri, &slot)) {
        httpd_resp_set_status(request, "404 Not Found");
        return send_text(request, "application/json", "{\"error\":\"invalid_rule_slot\"}\n");
    }
    char body[2048] = {0};
    if (receive_body(request, body, sizeof(body)) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *json = cJSON_ParseWithLength(body, strlen(body));
    b2_rule_t rule = {0};
    bool valid = json != NULL && json_rule_to_native(json, &rule);
    cJSON_Delete(json);
    if (!valid) {
        httpd_resp_set_status(request, "400 Bad Request");
        return send_text(request, "application/json", "{\"error\":\"invalid_rule\"}\n");
    }
    b2_settings_t settings = {0};
    esp_err_t err = b2_settings_load(&settings);
    if (err == ESP_OK) {
        settings.rules[slot] = rule;
        err = save_rules(&settings);
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return send_text(request, "application/json", "{\"error\":\"rule_save_failed\"}\n");
    }
    return send_text(request, "application/json", "{\"ok\":true,\"applied\":true}\n");
}

static esp_err_t rule_item_delete_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_control_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    uint8_t slot = 0;
    if (!parse_rule_slot(request->uri, &slot)) {
        httpd_resp_set_status(request, "404 Not Found");
        return send_text(request, "application/json", "{\"error\":\"invalid_rule_slot\"}\n");
    }
    b2_settings_t settings = {0};
    esp_err_t err = b2_settings_load(&settings);
    if (err == ESP_OK) {
        memset(&settings.rules[slot], 0, sizeof(settings.rules[slot]));
        err = save_rules(&settings);
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return send_text(request, "application/json", "{\"error\":\"rule_delete_failed\"}\n");
    }
    return send_text(request, "application/json", "{\"ok\":true,\"deleted\":true}\n");
}

static esp_err_t events_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_read_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    char body[2048] = {0};
    int length = snprintf(body, sizeof(body), "{\"schema\":1,\"count\":%lu,\"events\":[", (unsigned long)b2_event_log_count());
    if (length < 0 || length >= (int)sizeof(body)) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint8_t i = 0; i < 8 && i < b2_event_log_count(); ++i) {
        b2_event_t event = {0};
        if (b2_event_log_get_newest(i, &event) != ESP_OK) {
            break;
        }
        int written = snprintf(body + length, sizeof(body) - (size_t)length,
                                "%s{\"seq\":%lu,\"timestamp_us\":%lld,\"type\":%u,\"source\":%u,\"value\":%ld,\"text\":\"%s\"}",
                                i == 0 ? "" : ",", (unsigned long)event.sequence, (long long)event.timestamp_us,
                                event.type, event.source, (long)event.value, event.text);
        if (written < 0 || written >= (int)(sizeof(body) - (size_t)length)) {
            return ESP_ERR_INVALID_SIZE;
        }
        length += written;
    }
    int written = snprintf(body + length, sizeof(body) - (size_t)length, "]}\n");
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)(sizeof(body) - (size_t)length), ESP_ERR_INVALID_SIZE, TAG, "event response too large");
    return send_text(request, "application/json", body);
}

static esp_err_t self_test_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_control_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    bool relay1 = false;
    bool relay2 = false;
    b2_relay_get(0, &relay1);
    b2_relay_get(1, &relay2);
    char body[512] = {0};
    snprintf(body, sizeof(body), "{\"schema\":1,\"ok\":true,\"relay_driver\":true,\"relay1\":%s,\"relay2\":%s,\"event_log_count\":%lu,\"https\":true}\n",
             relay1 ? "true" : "false", relay2 ? "true" : "false", (unsigned long)b2_event_log_count());
    return send_text(request, "application/json", body);
}

static esp_err_t reboot_handler(httpd_req_t *request)
{
    esp_err_t auth_err = require_control_auth(request);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    esp_err_t flush_err = b2_event_log_flush();
    if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "event log flush before reboot failed: %s", esp_err_to_name(flush_err));
    }
    esp_err_t err = send_text(request, "application/json", "{\"ok\":true,\"rebooting\":true}\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return err;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    bool relay1 = false;
    bool relay2 = false;
    b2_relay_get(0, &relay1);
    b2_relay_get(1, &relay2);

    b2_modem_status_t modem = {0};
    b2_modem_get_status(&modem);
    b2_wifi_status_t wifi = {0};
    b2_wifi_get_status(&wifi);
    b2_mqtt_status_t mqtt = {0};
    b2_mqtt_get_status(&mqtt);

    char body[1536] = {0};
    int length = snprintf(body, sizeof(body),
                          "{\"device\":\"kincony-b2-compatible\",\"firmware\":\"esp-idf\","
                          "\"relay1\":%s,\"relay2\":%s,\"input1\":%s,\"input2\":%s,"
                          "\"wifi\":{\"started\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
                          "\"mqtt\":{\"started\":%s,\"connected\":%s,\"topic\":\"%s\"},"
                          "\"modem\":{\"registered\":%s,\"attached\":%s,\"csq\":%d}}\n",
                          relay1 ? "true" : "false", relay2 ? "true" : "false",
                          b2_input_is_active(0) ? "true" : "false", b2_input_is_active(1) ? "true" : "false",
                          wifi.started ? "true" : "false", wifi.connected ? "true" : "false",
                          wifi.ssid, wifi.ip, wifi.rssi, mqtt.started ? "true" : "false",
                          mqtt.connected ? "true" : "false", mqtt.base_topic, modem.registered ? "true" : "false",
                          modem.packet_attached ? "true" : "false", modem.signal_quality);
    ESP_RETURN_ON_FALSE(length > 0 && length < (int)sizeof(body), ESP_ERR_INVALID_SIZE, TAG, "status response too large");
    return send_text(request, "application/json", body);
}

esp_err_t b2_http_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    uint16_t port = 80;
    esp_err_t err = ESP_FAIL;
    (void)b2_tls_credentials_migrate_legacy_sd();
    if (b2_tls_credentials_load(s_server_certificate, sizeof(s_server_certificate),
                                s_server_private_key, sizeof(s_server_private_key)) == ESP_OK) {
        httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
        ssl_config.httpd.server_port = 443;
        ssl_config.httpd.max_uri_handlers = 18;
        ssl_config.httpd.uri_match_fn = httpd_uri_match_wildcard;
        ssl_config.servercert = (const uint8_t *)s_server_certificate;
        ssl_config.servercert_len = strlen(s_server_certificate) + 1U;
        ssl_config.prvtkey_pem = (const uint8_t *)s_server_private_key;
        ssl_config.prvtkey_len = strlen(s_server_private_key) + 1U;
        err = httpd_ssl_start(&s_server, &ssl_config);
        if (err == ESP_OK) {
            s_tls = true;
            port = 443;
        }
    }
    if (err != ESP_OK) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = 18;
        config.uri_match_fn = httpd_uri_match_wildcard;
        err = httpd_start(&s_server, &config);
        s_tls = false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP/HTTPS server start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t health_uri = {.uri = "/health", .method = HTTP_GET, .handler = health_handler, .user_ctx = NULL};
    const httpd_uri_t capabilities_uri = {.uri = "/api/v1/capabilities", .method = HTTP_GET, .handler = capabilities_handler, .user_ctx = NULL};
    const httpd_uri_t status_uri = {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};
    const httpd_uri_t relay1_write_uri = {.uri = "/api/v1/relay/1", .method = HTTP_POST, .handler = relay_write_handler, .user_ctx = NULL};
    const httpd_uri_t relay2_write_uri = {.uri = "/api/v1/relay/2", .method = HTTP_POST, .handler = relay_write_handler, .user_ctx = NULL};
    const httpd_uri_t rules_uri = {.uri = "/api/v1/rules", .method = HTTP_GET, .handler = rules_collection_handler, .user_ctx = NULL};
    const httpd_uri_t rule_get_uri = {.uri = "/api/v1/rules/*", .method = HTTP_GET, .handler = rule_item_get_handler, .user_ctx = NULL};
    const httpd_uri_t rule_put_uri = {.uri = "/api/v1/rules/*", .method = HTTP_PUT, .handler = rule_item_put_handler, .user_ctx = NULL};
    const httpd_uri_t rule_delete_uri = {.uri = "/api/v1/rules/*", .method = HTTP_DELETE, .handler = rule_item_delete_handler, .user_ctx = NULL};
    const httpd_uri_t events_uri = {.uri = "/api/v1/events", .method = HTTP_GET, .handler = events_handler, .user_ctx = NULL};
    const httpd_uri_t self_test_uri = {.uri = "/api/v1/self-test", .method = HTTP_GET, .handler = self_test_handler, .user_ctx = NULL};
    const httpd_uri_t reboot_uri = {.uri = "/api/v1/reboot", .method = HTTP_POST, .handler = reboot_handler, .user_ctx = NULL};

    err = httpd_register_uri_handler(s_server, &health_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &capabilities_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &status_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &relay1_write_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &relay2_write_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &rules_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &rule_get_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &rule_put_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &rule_delete_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &events_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &self_test_uri);
    if (err == ESP_OK && s_tls) err = httpd_register_uri_handler(s_server, &reboot_uri);
    if (err != ESP_OK) {
        if (s_tls) {
            httpd_ssl_stop(s_server);
        } else {
            httpd_stop(s_server);
        }
        s_server = NULL;
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "%s diagnostics listening on port %u; authenticated control and rule endpoints require HTTPS",
             s_tls ? "HTTPS" : "HTTP", port);
    return ESP_OK;
}

esp_err_t b2_http_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    esp_err_t err = s_tls ? httpd_ssl_stop(s_server) : httpd_stop(s_server);
    s_server = NULL;
    s_started = false;
    s_tls = false;
    return err;
}

esp_err_t b2_http_get_status(b2_http_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");
    status->started = s_started;
    status->tls = s_tls;
    status->port = s_tls ? 443 : 80;
    return ESP_OK;
}
