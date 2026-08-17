#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_ethernet_start(void);
bool b2_ethernet_is_started(void);

#ifdef __cplusplus
}
#endif
