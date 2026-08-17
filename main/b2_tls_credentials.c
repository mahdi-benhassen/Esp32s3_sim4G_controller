#include "b2_tls_credentials.h"

#include "b2_storage.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "b2_tls_creds";
static const char *NVS_NAMESPACE = "b2tls";
static const char *CERT_KEY = "certificate";
static const char *PRIVATE_KEY = "private_key";

static bool bounded_pem(const char *value, size_t capacity)
{
    if (value == NULL || strnlen(value, capacity) >= capacity) {
        return false;
    }
    return strstr(value, "-----BEGIN") != NULL && strstr(value, "-----END") != NULL;
}

static bool valid_certificate(const char *value, size_t capacity)
{
    return bounded_pem(value, capacity) && strstr(value, "-----BEGIN CERTIFICATE-----") != NULL;
}

static bool valid_private_key(const char *value, size_t capacity)
{
    if (!bounded_pem(value, capacity)) {
        return false;
    }
    return strstr(value, "-----BEGIN PRIVATE KEY-----") != NULL ||
           strstr(value, "-----BEGIN RSA PRIVATE KEY-----") != NULL ||
           strstr(value, "-----BEGIN EC PRIVATE KEY-----") != NULL;
}

esp_err_t b2_tls_credentials_store(const char *certificate, const char *private_key)
{
    ESP_RETURN_ON_FALSE(valid_certificate(certificate, B2_TLS_CREDENTIAL_MAX), ESP_ERR_INVALID_ARG, TAG,
                        "invalid certificate PEM");
    ESP_RETURN_ON_FALSE(valid_private_key(private_key, B2_TLS_CREDENTIAL_MAX), ESP_ERR_INVALID_ARG, TAG,
                        "invalid private-key PEM");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open TLS credential namespace");
    esp_err_t err = nvs_set_blob(handle, CERT_KEY, certificate, strlen(certificate) + 1U);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, PRIVATE_KEY, private_key, strlen(private_key) + 1U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t b2_tls_credentials_load(char *certificate, size_t certificate_size,
                                  char *private_key, size_t private_key_size)
{
    ESP_RETURN_ON_FALSE(certificate != NULL && private_key != NULL &&
                        certificate_size >= B2_TLS_CREDENTIAL_MAX && private_key_size >= B2_TLS_CREDENTIAL_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "credential buffers are too small");
    certificate[0] = '\0';
    private_key[0] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    }
    size_t cert_len = certificate_size;
    size_t key_len = private_key_size;
    err = nvs_get_blob(handle, CERT_KEY, certificate, &cert_len);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, PRIVATE_KEY, private_key, &key_len);
    }
    nvs_close(handle);
    if (err != ESP_OK || !valid_certificate(certificate, certificate_size) ||
        !valid_private_key(private_key, private_key_size)) {
        certificate[0] = '\0';
        private_key[0] = '\0';
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t b2_tls_credentials_migrate_legacy_sd(void)
{
    char certificate[B2_TLS_CREDENTIAL_MAX] = {0};
    char private_key[B2_TLS_CREDENTIAL_MAX] = {0};
    esp_err_t err = b2_tls_credentials_load(certificate, sizeof(certificate), private_key, sizeof(private_key));
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (!b2_storage_is_mounted()) {
        return ESP_ERR_NOT_FOUND;
    }
    err = b2_storage_read_text("server.crt", certificate, sizeof(certificate));
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    err = b2_storage_read_text("server.key", private_key, sizeof(private_key));
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(b2_tls_credentials_store(certificate, private_key), TAG, "migrate TLS credentials");
    esp_err_t cert_delete = b2_storage_delete_text("server.crt");
    esp_err_t key_delete = b2_storage_delete_text("server.key");
    if (cert_delete != ESP_OK || key_delete != ESP_OK) {
        ESP_LOGW(TAG, "TLS credentials migrated, but legacy SD cleanup was incomplete");
    }
    return ESP_OK;
}
