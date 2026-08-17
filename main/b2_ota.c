#include "b2_ota.h"

#include "esp_check.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "b2_ota";

bool b2_ota_is_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return false;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t b2_ota_confirm_running(void)
{
    if (!b2_ota_is_pending_verify()) {
        return ESP_OK;
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "running image confirmed; rollback cancelled");
    }
    return err;
}

esp_err_t b2_ota_start_https(const char *url, const char *ca_certificate)
{
    ESP_RETURN_ON_FALSE(url != NULL && strncmp(url, "https://", 8) == 0, ESP_ERR_INVALID_ARG, TAG, "HTTPS URL required");
    ESP_RETURN_ON_FALSE(ca_certificate != NULL && ca_certificate[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "CA certificate required");
    esp_http_client_config_t http_config = {
        .url = url,
        .cert_pem = ca_certificate,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = false,
    };
    ESP_LOGI(TAG, "starting verified HTTPS OTA from %s", url);
    esp_err_t err = esp_https_ota(&ota_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OTA image installed; rebooting for rollback verification");
    esp_restart();
    return ESP_OK;
}
