#pragma once

#include "esp_err.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_rtc_init(void);
esp_err_t b2_rtc_get_time(struct tm *timeinfo);
esp_err_t b2_rtc_set_time(const struct tm *timeinfo);

#ifdef __cplusplus
}
#endif
