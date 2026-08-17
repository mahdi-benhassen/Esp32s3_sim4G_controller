#pragma once

#include "b2_eventlog.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    uint8_t condition;
    uint8_t source;
    uint8_t action;
    uint8_t target;
    bool action_state;
    float threshold;
    uint32_t duration_ms;
    const char *sms_number;
} b2_core_rule_t;

/** Validate a rule using the same bounds as the firmware rule engine. */
bool b2_core_rule_is_valid(const b2_core_rule_t *rule, uint8_t relay_count,
                           uint8_t input_count, uint8_t analog_count);

/** Constant-time comparison for bounded textual secrets. */
bool b2_core_token_equal(const char *actual, const char *expected);

/** Extract an SMS command after validating the optional TOKEN prefix. */
int b2_core_extract_sms_command(const char *shared_secret, const char *message,
                                char *command, size_t command_size);

/** Return true when a persisted schema version is supported by migration. */
bool b2_core_settings_version_supported(uint32_t version, uint32_t current_version);

typedef struct {
    uint32_t next;
    uint32_t count;
    b2_event_t entries[B2_EVENT_LOG_CAPACITY];
} b2_core_event_ring_t;

void b2_core_event_ring_reset(b2_core_event_ring_t *ring);
esp_err_t b2_core_event_ring_append(b2_core_event_ring_t *ring, int64_t timestamp_us,
                                    uint8_t type, uint8_t source, int32_t value,
                                    const char *text, uint32_t *sequence);
esp_err_t b2_core_event_ring_get_newest(const b2_core_event_ring_t *ring,
                                        uint8_t index, b2_event_t *event);

#ifdef __cplusplus
}
#endif
