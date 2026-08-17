#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the application watchdog policy. */
esp_err_t b2_runtime_init(void);
/** Feed the watchdog from a healthy application task. */
esp_err_t b2_runtime_feed_watchdog(void);

#ifdef __cplusplus
}
#endif
