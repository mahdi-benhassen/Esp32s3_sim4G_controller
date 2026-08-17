#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Confirm the running image after successful service initialization. */
esp_err_t b2_ota_confirm_running(void);
/** Download and install a signed/verified image over HTTPS, then reboot. */
esp_err_t b2_ota_start_https(const char *url, const char *ca_certificate);
/** Report whether the bootloader has marked the image pending verification. */
bool b2_ota_is_pending_verify(void);

#ifdef __cplusplus
}
#endif
