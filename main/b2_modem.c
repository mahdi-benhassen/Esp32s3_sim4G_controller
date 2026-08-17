#include "b2_modem.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "b2_modem";
static SemaphoreHandle_t s_lock;
static b2_modem_sms_callback_t s_sms_callback;
static void *s_sms_context;
static b2_modem_status_t s_status;

static void copy_string(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, size, "%s", source);
}

static void modem_power_cycle(void)
{
    const b2_board_config_t *cfg = b2_config_get();
    gpio_set_level(cfg->modem_reset, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(cfg->modem_reset, 1);
    gpio_set_level(cfg->modem_power, 1);
    vTaskDelay(pdMS_TO_TICKS(1200));
    gpio_set_level(cfg->modem_power, 0);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

static void parse_unsolicited_line(const char *line, char *sender, size_t sender_size, bool *awaiting_sms)
{
    if (strncmp(line, "+CMT:", 5) == 0) {
        const char *first_quote = strchr(line, '"');
        const char *second_quote = first_quote != NULL ? strchr(first_quote + 1, '"') : NULL;
        if (first_quote != NULL && second_quote != NULL) {
            char temporary[32] = {0};
            const size_t count = (size_t)(second_quote - first_quote - 1);
            if (count < sizeof(temporary)) {
                memcpy(temporary, first_quote + 1, count);
                copy_string(sender, sender_size, temporary);
            }
        }
        *awaiting_sms = true;
    } else if (strncmp(line, "+CREG:", 6) == 0) {
        int n = 0, stat = 0;
        if (sscanf(line, "+CREG: %d,%d", &n, &stat) == 2) {
            s_status.registered = stat == 1 || stat == 5;
        }
    } else if (strncmp(line, "+CGATT:", 7) == 0) {
        int attached = 0;
        if (sscanf(line, "+CGATT: %d", &attached) == 1) {
            s_status.packet_attached = attached == 1;
        }
    } else if (strncmp(line, "+CSQ:", 5) == 0) {
        int rssi = 99;
        if (sscanf(line, "+CSQ: %d", &rssi) >= 1) {
            s_status.signal_quality = rssi;
        }
    }
}

static void modem_unsolicited_task(void *arg)
{
    (void)arg;
    const b2_board_config_t *cfg = b2_config_get();
    char line[256] = {0};
    size_t length = 0;
    char sender[32] = {0};
    bool awaiting_sms = false;

    for (;;) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint8_t byte = 0;
            const int count = uart_read_bytes(cfg->modem_uart, &byte, 1, pdMS_TO_TICKS(50));
            xSemaphoreGive(s_lock);
            if (count != 1) {
                continue;
            }
            if (byte == '\r') {
                continue;
            }
            if (byte == '\n') {
                line[length] = '\0';
                if (length > 0) {
                    if (awaiting_sms) {
                        if (s_sms_callback != NULL) {
                            s_sms_callback(sender, line, s_sms_context);
                        }
                        awaiting_sms = false;
                        sender[0] = '\0';
                    } else {
                        parse_unsolicited_line(line, sender, sizeof(sender), &awaiting_sms);
                    }
                }
                length = 0;
                continue;
            }
            if (length + 1 < sizeof(line)) {
                line[length++] = (char)byte;
            } else {
                length = 0;
            }
        }
    }
}

esp_err_t b2_modem_start(b2_modem_sms_callback_t sms_callback, void *context)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "create modem mutex");
    s_sms_callback = sms_callback;
    s_sms_context = context;
    s_status.uart_ready = true;
    s_status.signal_quality = 99;
    modem_power_cycle();
    if (xTaskCreate(modem_unsolicited_task, "b2_modem_rx", 4096, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    char response[256] = {0};
    ESP_RETURN_ON_ERROR(b2_modem_command("ATE0", response, sizeof(response), 2000), TAG, "disable modem echo");
    b2_modem_command("AT+CMGF=1", response, sizeof(response), 2000);
    b2_modem_command("AT+CNMI=2,2,0,0,0", response, sizeof(response), 2000);
    return ESP_OK;
}

esp_err_t b2_modem_command(const char *command, char *response, size_t response_size, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(command != NULL, ESP_ERR_INVALID_ARG, TAG, "null modem command");
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, TAG, "modem not initialized");
    const b2_board_config_t *cfg = b2_config_get();
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uart_flush_input(cfg->modem_uart);
    char command_line[256] = {0};
    snprintf(command_line, sizeof(command_line), "%s\r", command);
    uart_write_bytes(cfg->modem_uart, command_line, strlen(command_line));

    size_t used = 0;
    const int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);
    esp_err_t result = ESP_ERR_TIMEOUT;
    if (response != NULL && response_size > 0) {
        response[0] = '\0';
    }
    char line[256] = {0};
    size_t line_length = 0;
    while (esp_timer_get_time() < deadline) {
        uint8_t byte = 0;
        if (uart_read_bytes(cfg->modem_uart, &byte, 1, pdMS_TO_TICKS(20)) != 1) {
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte != '\n' && line_length + 1 < sizeof(line)) {
            line[line_length++] = (char)byte;
            continue;
        }
        if (byte == '\n' && line_length > 0) {
            line[line_length] = '\0';
            if (response != NULL && response_size > 1) {
                const size_t remaining = response_size - used - 1;
                if (remaining > 0) {
                    const int written = snprintf(response + used, remaining + 1, "%s\n", line);
                    if (written > 0) {
                        used += (size_t)written < remaining ? (size_t)written : remaining;
                    }
                }
            }
            if (strcmp(line, "OK") == 0) {
                result = ESP_OK;
                break;
            }
            if (strcmp(line, "ERROR") == 0) {
                result = ESP_FAIL;
                break;
            }
            line_length = 0;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t b2_modem_send_sms(const char *number, const char *message)
{
    ESP_RETURN_ON_FALSE(number != NULL && message != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid SMS arguments");
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, TAG, "modem not initialized");
    const b2_board_config_t *cfg = b2_config_get();
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    char command[96] = {0};
    snprintf(command, sizeof(command), "AT+CMGS=\\\"%s\\\"\\r", number);
    uart_flush_input(cfg->modem_uart);
    uart_write_bytes(cfg->modem_uart, command, strlen(command));
    bool prompt = false;
    const int64_t prompt_deadline = esp_timer_get_time() + 3000000;
    while (esp_timer_get_time() < prompt_deadline) {
        uint8_t byte = 0;
        if (uart_read_bytes(cfg->modem_uart, &byte, 1, pdMS_TO_TICKS(20)) == 1 && byte == '>') {
            prompt = true;
            break;
        }
    }
    if (!prompt) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }
    uart_write_bytes(cfg->modem_uart, message, strlen(message));
    const uint8_t ctrl_z = 0x1A;
    uart_write_bytes(cfg->modem_uart, (const char *)&ctrl_z, 1);
    char response[256] = {0};
    size_t used = 0;
    const int64_t response_deadline = esp_timer_get_time() + 15000000;
    esp_err_t result = ESP_ERR_TIMEOUT;
    while (esp_timer_get_time() < response_deadline) {
        uint8_t byte = 0;
        if (uart_read_bytes(cfg->modem_uart, &byte, 1, pdMS_TO_TICKS(50)) != 1) {
            continue;
        }
        if (used + 1 < sizeof(response)) {
            response[used++] = (char)byte;
            response[used] = '\0';
        }
        if (strstr(response, "OK") != NULL) {
            result = ESP_OK;
            break;
        }
        if (strstr(response, "ERROR") != NULL) {
            result = ESP_FAIL;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t b2_modem_dial(const char *number)
{
    ESP_RETURN_ON_FALSE(number != NULL, ESP_ERR_INVALID_ARG, TAG, "null dial number");
    char command[96] = {0};
    snprintf(command, sizeof(command), "ATD%s;", number);
    return b2_modem_command(command, NULL, 0, 5000);
}

esp_err_t b2_modem_hangup(void)
{
    return b2_modem_command("ATH", NULL, 0, 3000);
}

esp_err_t b2_modem_get_status(b2_modem_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "null status");
    char response[256] = {0};
    b2_modem_command("AT+CSQ", response, sizeof(response), 2000);
    b2_modem_command("AT+CREG?", response, sizeof(response), 2000);
    b2_modem_command("AT+CGATT?", response, sizeof(response), 2000);
    *status = s_status;
    return ESP_OK;
}
