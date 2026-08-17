#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_adc_init(void);
esp_err_t b2_adc_read_raw(uint8_t channel, int16_t *raw);
esp_err_t b2_adc_read_voltage(uint8_t channel, float *volts);
esp_err_t b2_adc_read_4_20ma(uint8_t channel, float *milliamps);

#ifdef __cplusplus
}
#endif
