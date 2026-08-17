#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool tls;
    uint16_t port;
} b2_http_status_t;

/** Start the HTTPS diagnostics and authenticated control service. */
esp_err_t b2_http_start(void);

/** Stop the HTTP diagnostics service. */
esp_err_t b2_http_stop(void);

/** Return the current HTTP service status. */
esp_err_t b2_http_get_status(b2_http_status_t *status);

#ifdef __cplusplus
}
#endif

/**
 * Public diagnostics endpoints:
 *   GET /health
 *   GET /api/v1/capabilities
 *   GET /api/v1/status
 *
 * When TLS credentials and a Bearer token are available, authenticated endpoints
 * expose relay control, event export, self-test, reboot, and rule CRUD:
 *   GET /api/v1/rules
 *   GET /api/v1/rules/{1..8}
 *   PUT /api/v1/rules/{1..8}
 *   DELETE /api/v1/rules/{1..8}
 *
 * Rule writes are validated, persisted to encrypted NVS, and live-reloaded. A
 * reboot request flushes the deferred event log before restarting. Plain HTTP
 * never exposes state-changing endpoints.
 */
