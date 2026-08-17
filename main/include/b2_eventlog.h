#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define B2_EVENT_LOG_CAPACITY 32
#define B2_EVENT_TEXT_MAX 64

typedef enum {
    B2_EVENT_BOOT = 1,
    B2_EVENT_INPUT = 2,
    B2_EVENT_RELAY = 3,
    B2_EVENT_SMS = 4,
    B2_EVENT_RULE = 5,
    B2_EVENT_NETWORK = 6,
    B2_EVENT_ALARM = 7,
} b2_event_type_t;

typedef struct {
    uint32_t sequence;
    int64_t timestamp_us;
    uint8_t type;
    uint8_t source;
    int32_t value;
    char text[B2_EVENT_TEXT_MAX];
} b2_event_t;

esp_err_t b2_event_log_init(void);
esp_err_t b2_event_log_append(b2_event_type_t type, uint8_t source, int32_t value, const char *text);
esp_err_t b2_event_log_flush(void);
esp_err_t b2_event_log_get_newest(uint8_t index, b2_event_t *event);
uint32_t b2_event_log_count(void);

#ifdef __cplusplus
}
#endif
