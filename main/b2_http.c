#include "b2_http.h"

#include "b2_inputs.h"
#include "b2_modem.h"
#include "b2_mqtt.h"
#include "b2_relay.h"
#include "b2_wifi.h"
#include "esp_http_server.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_http";
static httpd_handle_t s_server;
static bool s_started;

static esp_err_t send_text(httpd_req_t *request, const char *content_type, const char *body)
{
    ESP_RETURN_ON_FALSE(request != NULL && body != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid HTTP response");
    httpd_resp_set_type(request, content_type);
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
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
        "\"mqtt\",\"homeassistant-discovery\",\"sim7600\",\"gnss\"]}\n";
    return send_text(request, "application/json", capabilities);
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

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t health_uri = {
        .uri = "/health",
        .method = HTTP_GET,
        .handler = health_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t capabilities_uri = {
        .uri = "/api/v1/capabilities",
        .method = HTTP_GET,
        .handler = capabilities_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/v1/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
    };

    err = httpd_register_uri_handler(s_server, &health_uri);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_server, &capabilities_uri);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_server, &status_uri);
    }
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "read-only HTTP diagnostics listening on port %u", config.server_port);
    return ESP_OK;
}

esp_err_t b2_http_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    s_started = false;
    return err;
}

esp_err_t b2_http_get_status(b2_http_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");
    status->started = s_started;
    status->port = 80;
    return ESP_OK;
}
