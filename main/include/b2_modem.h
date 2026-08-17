#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*b2_modem_sms_callback_t)(const char *sender, const char *message, void *context);

typedef struct {
    bool uart_ready;
    bool registered;
    bool packet_attached;
    int signal_quality;
} b2_modem_status_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t b2_modem_start(b2_modem_sms_callback_t sms_callback, void *context);
esp_err_t b2_modem_command(const char *command, char *response, size_t response_size, uint32_t timeout_ms);
esp_err_t b2_modem_send_sms(const char *number, const char *message);
esp_err_t b2_modem_dial(const char *number);
esp_err_t b2_modem_hangup(void);
esp_err_t b2_modem_get_status(b2_modem_status_t *status);

#ifdef __cplusplus
}
#endif
