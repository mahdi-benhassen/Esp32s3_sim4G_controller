#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    B2_BUTTON_RESET = 0,
    B2_BUTTON_DOWNLOAD,
    B2_BUTTON_CONFIG,
    B2_BUTTON_COUNT,
} b2_button_id_t;

typedef void (*b2_button_callback_t)(b2_button_id_t button, bool pressed, void *context);

/** Start polling and debouncing configured physical buttons. */
esp_err_t b2_buttons_start(b2_button_callback_t callback, void *context);

/** Return the last debounced state of a configured button. */
bool b2_button_is_pressed(b2_button_id_t button);

#ifdef __cplusplus
}
#endif
