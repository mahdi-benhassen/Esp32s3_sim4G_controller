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
/** Configure runtime safety rules before any restored relay state is applied. */
esp_err_t b2_relay_configure_safety(bool interlock, bool fail_safe_off);
/** Apply the configured fail-safe output state. */
esp_err_t b2_relay_apply_safe_state(void);

#ifdef __cplusplus
}
#endif
