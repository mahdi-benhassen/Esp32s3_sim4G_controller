#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool connected;
    int8_t rssi;
    char ip[16];
    char ssid[33];
} b2_wifi_status_t;

/** Start Wi-Fi station mode when persisted credentials are enabled. */
esp_err_t b2_wifi_start(void);
esp_err_t b2_wifi_stop(void);
esp_err_t b2_wifi_get_status(b2_wifi_status_t *status);

#ifdef __cplusplus
}
#endif
