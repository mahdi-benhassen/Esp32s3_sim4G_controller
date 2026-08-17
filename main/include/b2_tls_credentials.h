#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define B2_TLS_CREDENTIAL_MAX 8192

/** Load the HTTPS certificate and private key from encrypted NVS. */
esp_err_t b2_tls_credentials_load(char *certificate, size_t certificate_size,
                                  char *private_key, size_t private_key_size);

/** Store validated HTTPS credentials in the encrypted NVS namespace. */
esp_err_t b2_tls_credentials_store(const char *certificate, const char *private_key);

/** Import legacy SD-card credentials once, then delete the plaintext files. */
esp_err_t b2_tls_credentials_migrate_legacy_sd(void);

#ifdef __cplusplus
}
#endif
