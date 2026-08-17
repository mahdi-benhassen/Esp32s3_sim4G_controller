#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/spi_common.h"
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define B2_RELAY_COUNT 2
#define B2_DRY_INPUT_COUNT 2
#define B2_ANALOG_COUNT 4
#define B2_ONEWIRE_COUNT 4

/*
 * These are conservative example assignments for an original ESP32-S3 carrier.
 * Replace them in b2_config.c or expose them through Kconfig when the PCB is
 * finalized. Never assume the KinCony product photos uniquely identify GPIOs.
 */
typedef struct {
    gpio_num_t relay[B2_RELAY_COUNT];
    gpio_num_t dry_input[B2_DRY_INPUT_COUNT];
    gpio_num_t button_reset;
    gpio_num_t button_download;
    gpio_num_t button_config;
    gpio_num_t one_wire[B2_ONEWIRE_COUNT];
    gpio_num_t modem_power;
    gpio_num_t modem_reset;
    gpio_num_t modem_ring;
    uart_port_t modem_uart;
    int modem_tx;
    int modem_rx;
    int modem_rts;
    int modem_cts;
    uart_port_t rs485_uart;
    int rs485_tx;
    int rs485_rx;
    int rs485_rts;
    i2c_port_t i2c_port;
    int i2c_sda;
    int i2c_scl;
    int i2c_frequency_hz;
    spi_host_device_t sd_spi_host;
    int sd_mosi;
    int sd_miso;
    int sd_sclk;
    int sd_cs;
    spi_host_device_t ethernet_spi_host;
    int ethernet_mosi;
    int ethernet_miso;
    int ethernet_sclk;
    int ethernet_cs;
    int ethernet_irq;
    int ethernet_reset;
    uint8_t i2c_expander_address;
    bool relay_active_high;
    bool dry_input_active_low;
    bool button_active_low;
} b2_board_config_t;

const b2_board_config_t *b2_config_get(void);
esp_err_t b2_config_validate(const b2_board_config_t *config);

#ifdef __cplusplus
}
#endif
