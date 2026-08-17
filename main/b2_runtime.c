#include "b2_runtime.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdbool.h>

static const char *TAG = "b2_runtime";
static bool s_initialized;

esp_err_t b2_runtime_init(void)
{
    esp_task_wdt_config_t config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
        .trigger_panic = false,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_init(&config);
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "task watchdog unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "watchdog policy active: timeout=%" PRIu32 " ms, recovery=degrade/reboot", config.timeout_ms);
    return ESP_OK;
}

esp_err_t b2_runtime_feed_watchdog(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_task_wdt_status(NULL);
    if (err == ESP_ERR_NOT_FOUND) {
        err = esp_task_wdt_add(NULL);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }
    return esp_task_wdt_reset();
}
