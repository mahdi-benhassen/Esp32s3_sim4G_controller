#include "b2_adc.h"
#include "b2_board.h"
#include "b2_console.h"
#include "b2_inputs.h"
#include "b2_modem.h"
#include "b2_oled.h"
#include "b2_relay.h"
#include "b2_rtc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_app";

static void input_event(uint8_t channel, bool active, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "dry input %u is %s", channel + 1, active ? "active" : "inactive");
}

static void sms_event(const char *sender, const char *message, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "SMS from %s: %s", sender != NULL ? sender : "unknown", message != NULL ? message : "empty");
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
        b2_relay_set((uint8_t)(relay - 1), true);
    } else if (sscanf(command, "RELAY%u OFF", &relay) == 1 && relay >= 1 && relay <= 2) {
        b2_relay_set((uint8_t)(relay - 1), false);
    } else if (sscanf(command, "RELAY%u TOGGLE", &relay) == 1 && relay >= 1 && relay <= 2) {
        b2_relay_toggle((uint8_t)(relay - 1));
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
        char line1[24] = {0};
        char line2[24] = {0};
        char line3[24] = {0};
        char line4[24] = {0};
        snprintf(line1, sizeof(line1), "B2 ESP32-S3");
        snprintf(line2, sizeof(line2), "R1:%s R2:%s", relay1 ? "ON" : "OFF", relay2 ? "ON" : "OFF");
        snprintf(line3, sizeof(line3), "4G:%s CSQ:%d", modem.registered ? "REG" : "----", modem.signal_quality);
        snprintf(line4, sizeof(line4), "IN1:%d IN2:%d", b2_input_is_active(0), b2_input_is_active(1));
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
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(b2_board_init());
    ESP_ERROR_CHECK(b2_relay_init());
    ESP_ERROR_CHECK(b2_inputs_start(input_event, NULL));

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
    ESP_LOGI(TAG, "B2-compatible controller ready");
}
