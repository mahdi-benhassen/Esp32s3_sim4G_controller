#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_relay_init(void);
esp_err_t b2_relay_set(uint8_t channel, bool on);
esp_err_t b2_relay_toggle(uint8_t channel);
esp_err_t b2_relay_get(uint8_t channel, bool *on);

#ifdef __cplusplus
}
#endif
