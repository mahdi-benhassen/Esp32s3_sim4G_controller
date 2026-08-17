#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the configured 1-Wire GPIO buses.
 *
 * Each configured channel is treated as an independent single-drop bus. The
 * carrier board must provide an external pull-up suitable for the sensor and
 * cable length.
 */
esp_err_t b2_onewire_init(void);

/** Return true when the selected channel has responded to a 1-Wire reset. */
esp_err_t b2_onewire_probe(uint8_t channel, bool *present);

/** Read a DS18B20 temperature in degrees Celsius from the selected channel. */
esp_err_t b2_onewire_read_celsius(uint8_t channel, float *temperature_c);

/** Read the 64-bit ROM identifier from a single-drop channel. */
esp_err_t b2_onewire_read_rom(uint8_t channel, uint64_t *rom_code);

#ifdef __cplusplus
}
#endif
