#include "b2_board.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "b2_board";

static esp_err_t configure_uart(uart_port_t port, int tx, int rx, int rts, int cts, int baud, bool rs485)
{
    const uart_config_t config = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(port, 4096, 4096, 0, NULL, 0), TAG, "install UART %d", port);
    ESP_RETURN_ON_ERROR(uart_param_config(port, &config), TAG, "configure UART %d", port);
    ESP_RETURN_ON_ERROR(uart_set_pin(port, tx, rx, rts, cts), TAG, "set UART %d pins", port);
    if (rs485) {
        ESP_RETURN_ON_ERROR(uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX), TAG, "set RS485 mode");
    }
    return ESP_OK;
}

esp_err_t b2_board_init(void)
{
    const b2_board_config_t *cfg = b2_config_get();
    ESP_RETURN_ON_ERROR(b2_config_validate(cfg), TAG, "invalid board config");

    gpio_config_t output = {
        .pin_bit_mask = (1ULL << cfg->relay[0]) | (1ULL << cfg->relay[1]) |
                        (1ULL << cfg->modem_power) | (1ULL << cfg->modem_reset),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output), TAG, "configure board outputs");
    gpio_set_level(cfg->relay[0], cfg->relay_active_high ? 0 : 1);
    gpio_set_level(cfg->relay[1], cfg->relay_active_high ? 0 : 1);
    gpio_set_level(cfg->modem_power, 0);
    gpio_set_level(cfg->modem_reset, 1);

    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << cfg->dry_input[0]) | (1ULL << cfg->dry_input[1]) |
                        (1ULL << cfg->modem_ring),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "configure board inputs");

    const i2c_config_t i2c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->i2c_sda,
        .scl_io_num = cfg->i2c_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = cfg->i2c_frequency_hz,
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(cfg->i2c_port, &i2c), TAG, "configure I2C");
    ESP_RETURN_ON_ERROR(i2c_driver_install(cfg->i2c_port, I2C_MODE_MASTER, 0, 0, 0), TAG, "install I2C");

    ESP_RETURN_ON_ERROR(configure_uart(cfg->modem_uart, cfg->modem_tx, cfg->modem_rx,
                                       cfg->modem_rts, cfg->modem_cts, 115200, false),
                        TAG, "install modem UART");
    ESP_RETURN_ON_ERROR(configure_uart(cfg->rs485_uart, cfg->rs485_tx, cfg->rs485_rx,
                                       cfg->rs485_rts, UART_PIN_NO_CHANGE, 115200, true),
                        TAG, "install RS485 UART");

    ESP_LOGI(TAG, "board initialized: I2C=%d modem UART=%d RS485 UART=%d",
             cfg->i2c_port, cfg->modem_uart, cfg->rs485_uart);
    return ESP_OK;
}
