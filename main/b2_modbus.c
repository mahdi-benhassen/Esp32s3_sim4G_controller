#include "b2_modbus.h"

#include "b2_config.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "b2_modbus";
static SemaphoreHandle_t s_lock;
static bool s_initialized;

static uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static esp_err_t validate_request(uint8_t slave, uint16_t count)
{
    ESP_RETURN_ON_FALSE(slave != 0 && slave <= 247, ESP_ERR_INVALID_ARG, TAG, "invalid Modbus slave");
    ESP_RETURN_ON_FALSE(count > 0 && count <= 125, ESP_ERR_INVALID_ARG, TAG, "invalid register count");
    return ESP_OK;
}

static esp_err_t read_exact(uart_port_t uart, uint8_t *buffer, size_t length, TickType_t timeout)
{
    size_t offset = 0;
    while (offset < length) {
        int received = uart_read_bytes(uart, buffer + offset, length - offset, timeout);
        if (received <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        offset += (size_t)received;
    }
    return ESP_OK;
}

static esp_err_t send_frame(const uint8_t *frame, size_t length)
{
    const b2_board_config_t *cfg = b2_config_get();
    uart_flush_input(cfg->rs485_uart);
    int written = uart_write_bytes(cfg->rs485_uart, frame, length);
    ESP_RETURN_ON_FALSE(written == (int)length, ESP_FAIL, TAG, "write Modbus request");
    ESP_RETURN_ON_ERROR(uart_wait_tx_done(cfg->rs485_uart, pdMS_TO_TICKS(100)), TAG, "wait Modbus TX");
    return ESP_OK;
}

esp_err_t b2_modbus_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    const b2_board_config_t *cfg = b2_config_get();
    ESP_RETURN_ON_FALSE(cfg->rs485_uart >= 0 && cfg->rs485_tx >= 0 && cfg->rs485_rx >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "RS485 UART is not configured");
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "create Modbus lock");
    s_initialized = true;
    ESP_LOGI(TAG, "Modbus RTU master ready on UART%d", cfg->rs485_uart);
    return ESP_OK;
}

esp_err_t b2_modbus_read_holding_registers(uint8_t slave, uint16_t address, uint16_t count,
                                           uint16_t *registers, size_t register_capacity)
{
    ESP_RETURN_ON_ERROR(validate_request(slave, count), TAG, "validate read request");
    ESP_RETURN_ON_FALSE(registers != NULL && register_capacity >= count, ESP_ERR_INVALID_SIZE, TAG, "register buffer too small");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Modbus not initialized");

    const b2_board_config_t *cfg = b2_config_get();
    uint8_t request[8] = {
        slave, 0x03, (uint8_t)(address >> 8), (uint8_t)address,
        (uint8_t)(count >> 8), (uint8_t)count, 0, 0,
    };
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = send_frame(request, sizeof(request));
    uint8_t header[3] = {0};
    uint8_t payload[252] = {0};
    if (err == ESP_OK) {
        err = read_exact(cfg->rs485_uart, header, sizeof(header), pdMS_TO_TICKS(500));
    }
    if (err == ESP_OK && header[0] != slave) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && header[1] == 0x83) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && header[1] != 0x03) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    size_t remaining = (err == ESP_OK) ? (size_t)header[2] + 2u : 0u;
    if (err == ESP_OK && (header[2] != count * 2u || remaining > sizeof(payload))) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        err = read_exact(cfg->rs485_uart, payload, remaining, pdMS_TO_TICKS(500));
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t frame[255] = {0};
    memcpy(frame, header, sizeof(header));
    memcpy(frame + sizeof(header), payload, remaining);
    uint16_t received_crc = (uint16_t)frame[3 + remaining - 2] | ((uint16_t)frame[3 + remaining - 1] << 8);
    ESP_RETURN_ON_FALSE(modbus_crc16(frame, 3 + remaining - 2) == received_crc, ESP_ERR_INVALID_CRC, TAG, "Modbus CRC mismatch");
    for (uint16_t i = 0; i < count; ++i) {
        registers[i] = ((uint16_t)payload[i * 2] << 8) | payload[i * 2 + 1];
    }
    return ESP_OK;
}

esp_err_t b2_modbus_write_single_register(uint8_t slave, uint16_t address, uint16_t value)
{
    ESP_RETURN_ON_ERROR(validate_request(slave, 1), TAG, "validate write request");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Modbus not initialized");

    const b2_board_config_t *cfg = b2_config_get();
    uint8_t request[8] = {
        slave, 0x06, (uint8_t)(address >> 8), (uint8_t)address,
        (uint8_t)(value >> 8), (uint8_t)value, 0, 0,
    };
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);
    uint8_t response[8] = {0};

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = send_frame(request, sizeof(request));
    if (err == ESP_OK) {
        err = read_exact(cfg->rs485_uart, response, sizeof(response), pdMS_TO_TICKS(500));
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return err;
    }
    ESP_RETURN_ON_FALSE(response[0] == slave && response[1] == 0x06, ESP_ERR_INVALID_RESPONSE, TAG, "invalid Modbus write response");
    uint16_t received_crc = (uint16_t)response[6] | ((uint16_t)response[7] << 8);
    ESP_RETURN_ON_FALSE(modbus_crc16(response, 6) == received_crc, ESP_ERR_INVALID_CRC, TAG, "Modbus CRC mismatch");
    ESP_RETURN_ON_FALSE(memcmp(response, request, 6) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "Modbus write echo mismatch");
    return ESP_OK;
}
