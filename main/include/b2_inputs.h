#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*b2_input_callback_t)(uint8_t channel, bool active, void *context);

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_inputs_start(b2_input_callback_t callback, void *context);
bool b2_input_is_active(uint8_t channel);

#ifdef __cplusplus
}
#endif
