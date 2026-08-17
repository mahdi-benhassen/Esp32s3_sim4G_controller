#include "b2_relay.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool s_state[B2_RELAY_COUNT];
static SemaphoreHandle_t s_lock;
static bool s_interlock;
static bool s_fail_safe_off = true;

esp_err_t b2_relay_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t b2_relay_configure_safety(bool interlock, bool fail_safe_off)
{
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, "b2_relay", "not initialized");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_interlock = interlock;
    s_fail_safe_off = fail_safe_off;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t b2_relay_apply_safe_state(void)
{
    if (!s_fail_safe_off) {
        return ESP_OK;
    }
    esp_err_t first_error = ESP_OK;
    for (uint8_t channel = 0; channel < B2_RELAY_COUNT; ++channel) {
        esp_err_t err = b2_relay_set(channel, false);
        if (first_error == ESP_OK && err != ESP_OK) {
            first_error = err;
        }
    }
    return first_error;
}

esp_err_t b2_relay_set(uint8_t channel, bool on)
{
    ESP_RETURN_ON_FALSE(channel < B2_RELAY_COUNT, ESP_ERR_INVALID_ARG, "b2_relay", "invalid relay channel");
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, "b2_relay", "not initialized");
    const b2_board_config_t *cfg = b2_config_get();
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (on && s_interlock) {
        const uint8_t other = (uint8_t)(channel == 0 ? 1 : 0);
        if (s_state[other]) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
    }
    s_state[channel] = on;
    const int level = on == cfg->relay_active_high ? 1 : 0;
    gpio_set_level(cfg->relay[channel], level);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t b2_relay_toggle(uint8_t channel)
{
    ESP_RETURN_ON_FALSE(channel < B2_RELAY_COUNT, ESP_ERR_INVALID_ARG, "b2_relay", "invalid relay channel");
    bool current = false;
    ESP_RETURN_ON_ERROR(b2_relay_get(channel, &current), "b2_relay", "read relay state");
    return b2_relay_set(channel, !current);
}

esp_err_t b2_relay_get(uint8_t channel, bool *on)
{
    ESP_RETURN_ON_FALSE(channel < B2_RELAY_COUNT && on != NULL, ESP_ERR_INVALID_ARG, "b2_relay", "invalid relay read");
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, "b2_relay", "not initialized");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *on = s_state[channel];
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
