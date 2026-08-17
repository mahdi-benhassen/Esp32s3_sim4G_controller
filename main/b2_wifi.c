#include "b2_wifi.h"

#include "b2_settings.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "b2_wifi";
static esp_netif_t *s_netif;
static bool s_initialized;
static bool s_started;
static bool s_connected;
static int8_t s_rssi;
static char s_ip[16] = "0.0.0.0";
static char s_ssid[B2_SETTINGS_WIFI_SSID_MAX];

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
        if (s_started) {
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        esp_netif_ip_info_t ip_info = {0};
        if (s_netif != NULL && esp_netif_get_ip_info(s_netif, &ip_info) == ESP_OK) {
            snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip_info.ip));
        }
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_rssi = ap.rssi;
        }
        ESP_LOGI(TAG, "Wi-Fi connected SSID=%s IP=%s RSSI=%d", s_ssid, s_ip, s_rssi);
    }
}

esp_err_t b2_wifi_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    b2_settings_t settings;
    esp_err_t settings_err = b2_settings_load(&settings);
    ESP_RETURN_ON_ERROR(settings_err, TAG, "load Wi-Fi settings");
    ESP_RETURN_ON_FALSE(settings.wifi_enabled && settings.wifi_ssid[0] != '\0', ESP_ERR_NOT_FOUND, TAG, "Wi-Fi credentials disabled or absent");

    if (!s_initialized) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "initialize Wi-Fi");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "register Wi-Fi events");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "register IP event");
        s_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_ERR_NO_MEM, TAG, "create Wi-Fi station netif");
        s_initialized = true;
    }

    wifi_config_t config = {0};
    size_t ssid_length = strnlen(settings.wifi_ssid, sizeof(config.sta.ssid) - 1);
    size_t password_length = strnlen(settings.wifi_password, sizeof(config.sta.password) - 1);
    memcpy(config.sta.ssid, settings.wifi_ssid, ssid_length);
    memcpy(config.sta.password, settings.wifi_password, password_length);
    config.sta.threshold.authmode = settings.wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "set Wi-Fi configuration");
    snprintf(s_ssid, sizeof(s_ssid), "%s", settings.wifi_ssid);
    s_started = true;
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
    return ESP_OK;
}

esp_err_t b2_wifi_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    s_started = false;
    s_connected = false;
    snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
    return esp_wifi_stop();
}

esp_err_t b2_wifi_get_status(b2_wifi_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "null Wi-Fi status");
    memset(status, 0, sizeof(*status));
    status->started = s_started;
    status->connected = s_connected;
    status->rssi = s_rssi;
    snprintf(status->ip, sizeof(status->ip), "%s", s_ip);
    snprintf(status->ssid, sizeof(status->ssid), "%s", s_ssid);
    return ESP_OK;
}
