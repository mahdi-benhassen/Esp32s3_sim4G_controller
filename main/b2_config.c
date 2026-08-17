#include "b2_config.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/spi_common.h"

static const char *TAG = "b2_config";

static const b2_board_config_t s_board = {
    .relay = {GPIO_NUM_4, GPIO_NUM_5},
    .dry_input = {GPIO_NUM_6, GPIO_NUM_7},
    .button_reset = GPIO_NUM_0,
    .button_download = GPIO_NUM_46,
    .button_config = GPIO_NUM_45,
    .one_wire = {GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11},
    .modem_power = GPIO_NUM_12,
    .modem_reset = GPIO_NUM_13,
    .modem_ring = GPIO_NUM_14,
    .modem_uart = UART_NUM_1,
    .modem_tx = 17,
    .modem_rx = 18,
    .modem_rts = UART_PIN_NO_CHANGE,
    .modem_cts = UART_PIN_NO_CHANGE,
    .rs485_uart = UART_NUM_2,
    .rs485_tx = 15,
    .rs485_rx = 16,
    .rs485_rts = 21,
    .i2c_port = I2C_NUM_0,
    .i2c_sda = 38,
    .i2c_scl = 39,
    .i2c_frequency_hz = 400000,
    .sd_spi_host = SPI2_HOST,
    .sd_mosi = 35,
    .sd_miso = 36,
    .sd_sclk = 37,
    .sd_cs = 34,
    .relay_active_high = true,
    .dry_input_active_low = true,
};

const b2_board_config_t *b2_config_get(void)
{
    return &s_board;
}

static bool pin_is_reserved_boot_pin(gpio_num_t pin)
{
    return pin == GPIO_NUM_0 || pin == GPIO_NUM_45 || pin == GPIO_NUM_46;
}

esp_err_t b2_config_validate(const b2_board_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "null board config");
    for (size_t i = 0; i < B2_RELAY_COUNT; ++i) {
        ESP_RETURN_ON_FALSE(config->relay[i] >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid relay GPIO");
    }
    for (size_t i = 0; i < B2_DRY_INPUT_COUNT; ++i) {
        ESP_RETURN_ON_FALSE(config->dry_input[i] >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid input GPIO");
    }
    ESP_RETURN_ON_FALSE(config->i2c_sda >= 0 && config->i2c_scl >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid I2C pins");
    ESP_RETURN_ON_FALSE(config->modem_tx >= 0 && config->modem_rx >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid modem UART pins");
    ESP_RETURN_ON_FALSE(config->rs485_tx >= 0 && config->rs485_rx >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid RS485 UART pins");

    for (size_t i = 0; i < B2_RELAY_COUNT; ++i) {
        ESP_RETURN_ON_FALSE(!pin_is_reserved_boot_pin(config->relay[i]), ESP_ERR_INVALID_ARG, TAG, "relay uses boot GPIO");
    }
    return ESP_OK;
}
