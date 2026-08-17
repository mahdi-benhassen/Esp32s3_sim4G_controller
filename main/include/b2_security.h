#pragma once

#include "b2_settings.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the default NVS partition with keys from the nvs_keys partition. */
esp_err_t b2_security_init_nvs(void);

/** Return true when a configured SMS sender/body passes production policy. */
bool b2_security_sms_accept(const b2_settings_t *settings, const char *sender, const char *message,
                            uint32_t now_ms);

/** Strip the optional TOKEN:<secret> prefix from an accepted SMS body. */
esp_err_t b2_security_sms_command(const b2_settings_t *settings, const char *message,
                                  char *command, size_t command_size);

/** Constant-time comparison for operator tokens. */
bool b2_security_token_equal(const char *actual, const char *expected);

/** Copy a secret into a masked printable representation. */
void b2_security_mask_secret(const char *secret, char *masked, size_t masked_size);

#ifdef __cplusplus
}
#endif
