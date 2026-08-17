#include "b2_console.h"

#include "b2_adc.h"
#include "b2_inputs.h"
#include "b2_modem.h"
#include "b2_relay.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_console";

static void print_help(void)
{
    printf("Commands: relay <1|2> <on|off|toggle>, input, adc <1..4>, modem, help\\n");
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96] = {0};
    print_help();
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            clearerr(stdin);
            continue;
        }
        unsigned channel = 0;
        char action[16] = {0};
        if (sscanf(line, "relay %u %15s", &channel, action) == 2 && channel >= 1 && channel <= 2) {
            if (strcmp(action, "on") == 0) {
                b2_relay_set((uint8_t)(channel - 1), true);
            } else if (strcmp(action, "off") == 0) {
                b2_relay_set((uint8_t)(channel - 1), false);
            } else if (strcmp(action, "toggle") == 0) {
                b2_relay_toggle((uint8_t)(channel - 1));
            } else {
                printf("Unknown relay action\\n");
            }
            continue;
        }
        if (strncmp(line, "input", 5) == 0) {
            printf("INPUT1=%s INPUT2=%s\\n", b2_input_is_active(0) ? "ON" : "OFF", b2_input_is_active(1) ? "ON" : "OFF");
            continue;
        }
        if (sscanf(line, "adc %u", &channel) == 1 && channel >= 1 && channel <= 4) {
            float value = 0.0f;
            if (channel <= 2) {
                if (b2_adc_read_voltage((uint8_t)(channel - 1), &value) == ESP_OK) {
                    printf("ADC%u=%.3f V\\n", channel, value);
                }
            } else if (b2_adc_read_4_20ma((uint8_t)(channel - 1), &value) == ESP_OK) {
                printf("ADC%u=%.3f mA\\n", channel, value);
            }
            continue;
        }
        if (strncmp(line, "modem", 5) == 0) {
            b2_modem_status_t status = {0};
            b2_modem_get_status(&status);
            printf("MODEM registered=%s attached=%s CSQ=%d\\n", status.registered ? "yes" : "no", status.packet_attached ? "yes" : "no", status.signal_quality);
            continue;
        }
        if (strncmp(line, "help", 4) == 0) {
            print_help();
            continue;
        }
        ESP_LOGW(TAG, "unrecognized command: %s", line);
    }
}

esp_err_t b2_console_start(void)
{
    return xTaskCreate(console_task, "b2_console", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
