#include "b2_adc.h"
#include "b2_board.h"
#include "b2_buttons.h"
#include "b2_console.h"
#include "b2_inputs.h"
#include "b2_http.h"
#include "b2_modem.h"
#include "b2_modbus.h"
#include "b2_mqtt.h"
#include "b2_oled.h"
#include "b2_onewire.h"
#include "b2_relay.h"
#include "b2_rtc.h"
#include "b2_settings.h"
#include "b2_storage.h"
#include "b2_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_app";
static b2_settings_t s_settings;

static void input_event(uint8_t channel, bool active, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "dry input %u is %s", channel + 1, active ? "active" : "inactive");
    if (b2_mqtt_publish_state() != ESP_OK) {
        ESP_LOGD(TAG, "MQTT input-state publication unavailable");
    }
}

static void button_event(b2_button_id_t button, bool pressed, void *context)
{
    (void)context;
    if (!pressed) {
        return;
    }
    ESP_LOGI(TAG, "button %d pressed", (int)button);
    if (button == B2_BUTTON_CONFIG) {
        ESP_LOGI(TAG, "configuration button pressed; use the local console for provisioning");
    } else if (button == B2_BUTTON_DOWNLOAD || button == B2_BUTTON_RESET) {
        ESP_LOGI(TAG, "boot/reset button event observed; ROM boot behavior remains hardware-controlled");
    }
}

static bool sms_sender_allowed(const char *sender)
{
    if (s_settings.sms_auth_mode == B2_SMS_AUTH_ALLOW_ALL) {
        return true;
    }
    if (sender == NULL) {
        return false;
    }
    for (size_t i = 0; i < s_settings.sms_allowlist_count; ++i) {
        if (strcmp(sender, s_settings.sms_allowlist[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void persist_relay_state(uint8_t channel, bool on)
{
    if (!s_settings.restore_relay_state || channel >= 2) {
        return;
    }
    s_settings.relay_state[channel] = on;
    if (b2_settings_save(&s_settings) != ESP_OK) {
        ESP_LOGW(TAG, "could not persist relay %u state", channel + 1);
    }
}

static void sms_event(const char *sender, const char *message, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "SMS from %s: %s", sender != NULL ? sender : "unknown", message != NULL ? message : "empty");
    if (!sms_sender_allowed(sender)) {
        ESP_LOGW(TAG, "ignoring SMS from unauthorized sender");
        return;
    }
    if (message == NULL) {
        return;
    }
    char command[64] = {0};
    for (size_t i = 0; i + 1 < sizeof(command) && message[i] != '\0'; ++i) {
        char c = message[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        command[i] = c;
    }
    unsigned relay = 0;
    if (sscanf(command, "RELAY%u ON", &relay) == 1 && relay >= 1 && relay <= 2) {
                    if (b2_relay_set((uint8_t)(relay - 1), true) == ESP_OK) {
                persist_relay_state((uint8_t)(relay - 1), true);
                b2_mqtt_publish_state();
            }

    } else if (sscanf(command, "RELAY%u OFF", &relay) == 1 && relay >= 1 && relay <= 2) {
                    if (b2_relay_set((uint8_t)(relay - 1), false) == ESP_OK) {
                persist_relay_state((uint8_t)(relay - 1), false);
                b2_mqtt_publish_state();
            }

    } else if (sscanf(command, "RELAY%u TOGGLE", &relay) == 1 && relay >= 1 && relay <= 2) {
        if (b2_relay_toggle((uint8_t)(relay - 1)) == ESP_OK) {
            bool state = false;
            if (b2_relay_get((uint8_t)(relay - 1), &state) == ESP_OK) {
                persist_relay_state((uint8_t)(relay - 1), state);
                b2_mqtt_publish_state();
            }
        }
    } else {
        ESP_LOGW(TAG, "unsupported SMS command");
    }
}

static void status_task(void *arg)
{
    (void)arg;
    for (;;) {
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
        char line1[24] = {0};
        char line2[24] = {0};
        char line3[24] = {0};
        char line4[24] = {0};
        snprintf(line1, sizeof(line1), "B2 ESP32-S3");
        snprintf(line2, sizeof(line2), "R1:%s R2:%s", relay1 ? "ON" : "OFF", relay2 ? "ON" : "OFF");
        snprintf(line3, sizeof(line3), "4G:%s CSQ:%d", modem.registered ? "REG" : "----", modem.signal_quality);
        snprintf(line4, sizeof(line4), "W:%s M:%s", wifi.connected ? "OK" : "--", mqtt.connected ? "OK" : "--");
        b2_oled_show_status(line1, line2, line3, line4);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(b2_settings_load(&s_settings));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(b2_board_init());
    ESP_ERROR_CHECK(b2_relay_init());
    if (s_settings.restore_relay_state) {
        b2_relay_set(0, s_settings.relay_state[0]);
        b2_relay_set(1, s_settings.relay_state[1]);
    }
    ESP_ERROR_CHECK(b2_inputs_start(input_event, NULL));
    if (b2_buttons_start(button_event, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "continuing without physical-button service");
    }
    if (b2_modbus_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without Modbus RTU service");
    }
    if (b2_onewire_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without 1-Wire service");
    }
    if (b2_storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without SD-card storage");
    }
    if (b2_wifi_start() != ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi station disabled or credentials unavailable");
    }
    if (b2_http_start() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without HTTP diagnostics service");
    }
    if (b2_mqtt_start() != ESP_OK) {
        ESP_LOGI(TAG, "MQTT disabled or broker configuration unavailable");
    }
    if (b2_adc_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without ADS1115");
    }
    if (b2_rtc_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without DS3231");
    }
    if (b2_oled_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without SSD1306");
    }
    if (b2_modem_start(sms_event, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "continuing without SIM7600");
    }
    ESP_ERROR_CHECK(b2_console_start());
    xTaskCreate(status_task, "b2_status", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "B2-compatible controller ready; SD=%s WIFI=%s MQTT=%s SMS_AUTH=%s",
             b2_storage_is_mounted() ? "mounted" : "absent",
             s_settings.wifi_enabled ? "enabled" : "disabled",
             s_settings.mqtt_enabled ? "enabled" : "disabled",
             s_settings.sms_auth_mode == B2_SMS_AUTH_ALLOWLIST ? "allowlist" : "allow-all");
}
