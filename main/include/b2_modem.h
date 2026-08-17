#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "b2_settings.h"

typedef void (*b2_modem_sms_callback_t)(const char *sender, const char *message, void *context);

typedef struct {
    bool uart_ready;
    bool registered;
    bool packet_attached;
    int signal_quality;
} b2_modem_status_t;

typedef struct {
    bool enabled;
    bool fix_valid;
    char utc[24];
    char latitude[20];
    char longitude[20];
    char altitude_m[16];
    char speed_knots[16];
    int satellites;
} b2_modem_gnss_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_modem_start(b2_modem_sms_callback_t sms_callback, void *context);
esp_err_t b2_modem_command(const char *command, char *response, size_t response_size, uint32_t timeout_ms);
esp_err_t b2_modem_send_sms(const char *number, const char *message);
esp_err_t b2_modem_dial(const char *number);
esp_err_t b2_modem_hangup(void);
esp_err_t b2_modem_get_status(b2_modem_status_t *status);
esp_err_t b2_modem_set_apn(const char *apn);
esp_err_t b2_modem_set_apn_auth(const char *apn, const char *username, const char *password, b2_apn_auth_type_t auth_type);
esp_err_t b2_modem_activate_pdp(void);
esp_err_t b2_modem_gnss_enable(bool enable);
esp_err_t b2_modem_gnss_get(b2_modem_gnss_t *gnss);

#ifdef __cplusplus
}
#endif
