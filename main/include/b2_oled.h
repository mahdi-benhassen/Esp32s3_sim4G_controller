#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_oled_init(void);
esp_err_t b2_oled_show_status(const char *line1, const char *line2, const char *line3, const char *line4);

#ifdef __cplusplus
}
#endif
