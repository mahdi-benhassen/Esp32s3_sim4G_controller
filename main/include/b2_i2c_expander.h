#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_i2c_expander_init(void);
esp_err_t b2_i2c_expander_select(uint8_t channel);
esp_err_t b2_i2c_expander_disable(void);
bool b2_i2c_expander_is_enabled(void);

#ifdef __cplusplus
}
#endif
