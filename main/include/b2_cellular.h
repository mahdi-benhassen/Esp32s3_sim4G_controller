#pragma once

#include "b2_settings.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    bool started;
    bool connected;
    bool retrying;
    char ip[16];
    uint32_t retry_count;
} b2_cellular_status_t;

esp_err_t b2_cellular_start(const b2_settings_t *settings);
esp_err_t b2_cellular_get_status(b2_cellular_status_t *status);

#ifdef __cplusplus
}
#endif
