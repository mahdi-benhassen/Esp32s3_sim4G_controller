#include "b2_http.h"

#include "b2_eventlog.h"
#include "b2_inputs.h"
#include "b2_modem.h"
#include "b2_mqtt.h"
#include "b2_relay.h"
#include "b2_security.h"
#include "b2_settings.h"
#include "b2_storage.h"
#include "b2_wifi.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_http";
static httpd_handle_t s_server;
static bool s_started;
static bool s_tls;
static char s_server_certificate[4096];
static char s_server_private_key[4096];
static int64_t s_last_control_us;

static esp_err_t send_text(httpd_req_t *request, const char *content_type, const char *body)
{
    ESP_RETURN_ON_FALSE(request != NULL && body != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid HTTP response");
    httpd_resp_set_type(request, content_type);
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
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

static esp_err_t health_handler(httpd_req_t *request)
{
    return send_text(request, "text/plain", "ok\n");
}

static esp_err_t capabilities_handler(httpd_req_t *request)
{
    static const char capabilities[] =
        "{\"device\":\"kincony-b2-compatible\",\"target\":\"esp32s3\","
        "\"api\":\"read-only\",\"features\":[\"relay\",\"dry-input\",\"analog\","
        "\"onewire\",\"rtc\",\"oled\",\"sd\",\"rs485-modbus\",\"wifi\","
        "\"mqtt\",\"homeassistant-discovery\",\"sim7600\",\"gnss\",\"rules\",\"time-sync\",\"ota\","
        "\"https\",\"authenticated-relay-write\",\"remote-diagnostics\",\"event-log\"]}\n";
    return send_text(request, "application/json", capabilities);
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
    if (!s_tls || !bearer_authorized(request)) {
        return unauthorized(request);
    }
    if (!control_rate_allowed()) {
        httpd_resp_set_status(request, "429 Too Many Requests");
        return send_text(request, "application/json", "{\"error\":\"rate_limited\"}\n");
    }
    return ESP_OK;
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
    int received = httpd_req_recv(request, body, sizeof(body) - 1U);
    if (received <= 0) {
        return send_text(request, "application/json", "{\"error\":\"empty_body\"}\n");
    }
    body[received] = '\0';
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

static esp_err_t events_handler(httpd_req_t *request)
{
    if (!s_tls || !bearer_authorized(request)) {
        return unauthorized(request);
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
    if (b2_storage_is_mounted() &&
        b2_storage_read_text("server.crt", s_server_certificate, sizeof(s_server_certificate)) == ESP_OK &&
        b2_storage_read_text("server.key", s_server_private_key, sizeof(s_server_private_key)) == ESP_OK) {
        httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
        ssl_config.httpd.server_port = 443;
        ssl_config.httpd.max_uri_handlers = 12;
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
        config.max_uri_handlers = 12;
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
    const httpd_uri_t events_uri = {.uri = "/api/v1/events", .method = HTTP_GET, .handler = events_handler, .user_ctx = NULL};
    const httpd_uri_t self_test_uri = {.uri = "/api/v1/self-test", .method = HTTP_GET, .handler = self_test_handler, .user_ctx = NULL};
    const httpd_uri_t reboot_uri = {.uri = "/api/v1/reboot", .method = HTTP_POST, .handler = reboot_handler, .user_ctx = NULL};

    err = httpd_register_uri_handler(s_server, &health_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &capabilities_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &status_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &relay1_write_uri);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &relay2_write_uri);
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
    ESP_LOGI(TAG, "%s diagnostics listening on port %u; relay writes require Bearer auth", s_tls ? "HTTPS" : "HTTP", port);
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
