#include "b2_rtc.h"

#include "b2_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define DS3231_ADDRESS 0x68

static const char *TAG = "b2_rtc";

static uint8_t bcd_to_bin(uint8_t value)
{
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t length)
{
    const b2_board_config_t *cfg = b2_config_get();
    return i2c_master_write_read_device(cfg->i2c_port, DS3231_ADDRESS, &reg, 1, data, length, pdMS_TO_TICKS(100));
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t length)
{
    const b2_board_config_t *cfg = b2_config_get();
    uint8_t buffer[8] = {0};
    if (length > sizeof(buffer) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = reg;
    for (size_t i = 0; i < length; ++i) {
        buffer[i + 1] = data[i];
    }
    return i2c_master_write_to_device(cfg->i2c_port, DS3231_ADDRESS, buffer, length + 1, pdMS_TO_TICKS(100));
}

esp_err_t b2_rtc_init(void)
{
    uint8_t status = 0;
    const esp_err_t err = rtc_read(0x0F, &status, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not detected at 0x%02x: %s", DS3231_ADDRESS, esp_err_to_name(err));
    }
    return err;
}

esp_err_t b2_rtc_get_time(struct tm *timeinfo)
{
    ESP_RETURN_ON_FALSE(timeinfo != NULL, ESP_ERR_INVALID_ARG, TAG, "null time output");
    uint8_t data[7] = {0};
    ESP_RETURN_ON_ERROR(rtc_read(0x00, data, sizeof(data)), TAG, "read RTC");
    timeinfo->tm_sec = bcd_to_bin(data[0] & 0x7F);
    timeinfo->tm_min = bcd_to_bin(data[1] & 0x7F);
    timeinfo->tm_hour = bcd_to_bin(data[2] & 0x3F);
    timeinfo->tm_wday = bcd_to_bin(data[3] & 0x07) - 1;
    timeinfo->tm_mday = bcd_to_bin(data[4] & 0x3F);
    timeinfo->tm_mon = bcd_to_bin(data[5] & 0x1F) - 1;
    timeinfo->tm_year = bcd_to_bin(data[6]) + 100;
    timeinfo->tm_isdst = -1;
    return ESP_OK;
}

esp_err_t b2_rtc_set_time(const struct tm *timeinfo)
{
    ESP_RETURN_ON_FALSE(timeinfo != NULL, ESP_ERR_INVALID_ARG, TAG, "null time input");
    const uint8_t data[7] = {
        bin_to_bcd((uint8_t)timeinfo->tm_sec),
        bin_to_bcd((uint8_t)timeinfo->tm_min),
        bin_to_bcd((uint8_t)timeinfo->tm_hour),
        bin_to_bcd((uint8_t)(timeinfo->tm_wday + 1)),
        bin_to_bcd((uint8_t)timeinfo->tm_mday),
        bin_to_bcd((uint8_t)(timeinfo->tm_mon + 1)),
        bin_to_bcd((uint8_t)(timeinfo->tm_year - 100)),
    };
    return rtc_write(0x00, data, sizeof(data));
}
