#include "b2_adc.h"

#include "b2_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ADS1115_ADDRESS 0x48
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 0x01
#define ADS1115_CONFIG_OS_SINGLE 0x8000
#define ADS1115_CONFIG_MUX_AIN0_GND 0x4000
#define ADS1115_CONFIG_PGA_4V096 0x0200
#define ADS1115_CONFIG_MODE_SINGLE 0x0100
#define ADS1115_CONFIG_DR_128SPS 0x0080
#define ADS1115_CONFIG_COMP_DISABLE 0x0003
#define B2_VOLTAGE_INPUT_SCALE 1.0f
#define B2_CURRENT_SHUNT_OHMS 165.0f

static const char *TAG = "b2_adc";

static esp_err_t ads_write(uint8_t reg, uint16_t value)
{
    const b2_board_config_t *cfg = b2_config_get();
    uint8_t data[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_write_to_device(cfg->i2c_port, ADS1115_ADDRESS, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t ads_read(uint8_t reg, uint8_t *data, size_t length)
{
    const b2_board_config_t *cfg = b2_config_get();
    return i2c_master_write_read_device(cfg->i2c_port, ADS1115_ADDRESS, &reg, 1, data, length, pdMS_TO_TICKS(100));
}

esp_err_t b2_adc_init(void)
{
    uint8_t probe[2] = {0};
    const esp_err_t err = ads_read(ADS1115_REG_CONFIG, probe, sizeof(probe));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADS1115 not detected at 0x%02x: %s", ADS1115_ADDRESS, esp_err_to_name(err));
    }
    return err;
}

esp_err_t b2_adc_read_raw(uint8_t channel, int16_t *raw)
{
    ESP_RETURN_ON_FALSE(channel < B2_ANALOG_COUNT && raw != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid ADC channel");
    const uint16_t mux = ADS1115_CONFIG_MUX_AIN0_GND - ((uint16_t)channel << 12);
    const uint16_t config = ADS1115_CONFIG_OS_SINGLE | mux | ADS1115_CONFIG_PGA_4V096 |
                            ADS1115_CONFIG_MODE_SINGLE | ADS1115_CONFIG_DR_128SPS | ADS1115_CONFIG_COMP_DISABLE;
    ESP_RETURN_ON_ERROR(ads_write(ADS1115_REG_CONFIG, config), TAG, "start ADC conversion");
    vTaskDelay(pdMS_TO_TICKS(9));
    uint8_t data[2] = {0};
    ESP_RETURN_ON_ERROR(ads_read(ADS1115_REG_CONVERSION, data, sizeof(data)), TAG, "read ADC conversion");
    *raw = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t b2_adc_read_voltage(uint8_t channel, float *volts)
{
    int16_t raw = 0;
    ESP_RETURN_ON_FALSE(volts != NULL, ESP_ERR_INVALID_ARG, TAG, "null voltage output");
    ESP_RETURN_ON_ERROR(b2_adc_read_raw(channel, &raw), TAG, "read voltage raw");
    *volts = ((float)raw * 4.096f / 32768.0f) * B2_VOLTAGE_INPUT_SCALE;
    return ESP_OK;
}

esp_err_t b2_adc_read_4_20ma(uint8_t channel, float *milliamps)
{
    ESP_RETURN_ON_FALSE(channel >= 2 && channel < B2_ANALOG_COUNT && milliamps != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "4-20mA uses channels 2 and 3");
    float volts = 0.0f;
    ESP_RETURN_ON_ERROR(b2_adc_read_voltage(channel, &volts), TAG, "read current voltage");
    *milliamps = (volts / B2_CURRENT_SHUNT_OHMS) * 1000.0f;
    return ESP_OK;
}
