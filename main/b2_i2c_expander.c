#include "b2_i2c_expander.h"

#include "b2_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"

#if CONFIG_B2_I2C_EXPANDER_ENABLED
static const char *TAG = "b2_i2c_mux";
static uint8_t s_address;
#endif
static bool s_enabled;

esp_err_t b2_i2c_expander_init(void)
{
#if !CONFIG_B2_I2C_EXPANDER_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    const b2_board_config_t *config = b2_config_get();
    ESP_RETURN_ON_FALSE(config != NULL && config->i2c_expander_address >= 0x08 && config->i2c_expander_address <= 0x77,
                        ESP_ERR_INVALID_ARG, TAG, "invalid PCA9548A address");
    s_address = config->i2c_expander_address;
    s_enabled = true;
    return b2_i2c_expander_disable();
#endif
}

esp_err_t b2_i2c_expander_select(uint8_t channel)
{
#if !CONFIG_B2_I2C_EXPANDER_ENABLED
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_enabled && channel < 8, ESP_ERR_INVALID_ARG, TAG, "invalid PCA9548A channel");
    const uint8_t control = (uint8_t)(1U << channel);
    return i2c_master_write_to_device(b2_config_get()->i2c_port, s_address, &control, 1, pdMS_TO_TICKS(100));
#endif
}

esp_err_t b2_i2c_expander_disable(void)
{
#if !CONFIG_B2_I2C_EXPANDER_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_enabled, ESP_ERR_INVALID_STATE, TAG, "PCA9548A not initialized");
    const uint8_t control = 0;
    return i2c_master_write_to_device(b2_config_get()->i2c_port, s_address, &control, 1, pdMS_TO_TICKS(100));
#endif
}

bool b2_i2c_expander_is_enabled(void)
{
    return s_enabled;
}
