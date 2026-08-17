#include "b2_relay.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool s_state[B2_RELAY_COUNT];
static SemaphoreHandle_t s_lock;

esp_err_t b2_relay_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t b2_relay_set(uint8_t channel, bool on)
{
    ESP_RETURN_ON_FALSE(channel < B2_RELAY_COUNT, ESP_ERR_INVALID_ARG, "b2_relay", "invalid relay channel");
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, "b2_relay", "not initialized");
    const b2_board_config_t *cfg = b2_config_get();
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
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
