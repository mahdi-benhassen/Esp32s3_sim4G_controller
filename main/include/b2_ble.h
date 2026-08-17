#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    bool advertising;
    bool physical_presence_required;
} b2_ble_status_t;

/** Start BLE commissioning only when enabled and the CONFIG button is held. */
esp_err_t b2_ble_start(void);

/** Stop BLE commissioning and release the controller/host resources. */
esp_err_t b2_ble_stop(void);

/** Return BLE commissioning status. */
esp_err_t b2_ble_get_status(b2_ble_status_t *status);

#ifdef __cplusplus
}
#endif
