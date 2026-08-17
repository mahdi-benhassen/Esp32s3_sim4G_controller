#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    uint16_t port;
} b2_http_status_t;

/** Start the read-only HTTP diagnostics service. */
esp_err_t b2_http_start(void);

/** Stop the HTTP diagnostics service. */
esp_err_t b2_http_stop(void);

/** Return the current HTTP service status. */
esp_err_t b2_http_get_status(b2_http_status_t *status);

#ifdef __cplusplus
}
#endif

/**
 * Endpoints exposed by the open diagnostics service:
 *   GET /health
 *   GET /api/v1/capabilities
 *   GET /api/v1/status
 *
 * The service intentionally does not expose relay write operations. It must
 * be treated as a LAN-only diagnostic interface until an authentication and
 * transport-security policy is configured for a production deployment.
 */
