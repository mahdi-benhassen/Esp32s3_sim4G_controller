#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Prepare the configured RS485 UART for Modbus RTU transactions. */
esp_err_t b2_modbus_init(void);

/** Read holding registers with function 0x03. */
esp_err_t b2_modbus_read_holding_registers(uint8_t slave, uint16_t address, uint16_t count,
                                           uint16_t *registers, size_t register_capacity);

/** Write one holding register with function 0x06. */
esp_err_t b2_modbus_write_single_register(uint8_t slave, uint16_t address, uint16_t value);

#ifdef __cplusplus
}
#endif
