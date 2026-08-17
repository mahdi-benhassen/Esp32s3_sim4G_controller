#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool synchronized;
    char timezone[40];
} b2_time_status_t;

esp_err_t b2_time_start(void);
esp_err_t b2_time_get_status(b2_time_status_t *status);

#ifdef __cplusplus
}
#endif
