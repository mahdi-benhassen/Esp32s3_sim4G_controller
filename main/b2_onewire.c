#include "b2_onewire.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "b2_onewire";
static SemaphoreHandle_t s_lock;
static bool s_initialized;

static esp_err_t validate_channel(uint8_t channel)
{
    ESP_RETURN_ON_FALSE(channel < B2_ONEWIRE_COUNT, ESP_ERR_INVALID_ARG, TAG, "invalid 1-Wire channel");
    const b2_board_config_t *cfg = b2_config_get();
    ESP_RETURN_ON_FALSE(cfg->one_wire[channel] >= 0, ESP_ERR_INVALID_ARG, TAG, "1-Wire channel is not configured");
    return ESP_OK;
}

static void release_line(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

static void drive_low(gpio_num_t pin)
{
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

static bool read_line(gpio_num_t pin)
{
    return gpio_get_level(pin) != 0;
}

static bool one_wire_reset(gpio_num_t pin)
{
    drive_low(pin);
    esp_rom_delay_us(480);
    release_line(pin);
    esp_rom_delay_us(70);
    bool present = !read_line(pin);
    esp_rom_delay_us(410);
    return present;
}

static void one_wire_write_bit(gpio_num_t pin, bool bit)
{
    if (bit) {
        drive_low(pin);
        esp_rom_delay_us(6);
        release_line(pin);
        esp_rom_delay_us(64);
    } else {
        drive_low(pin);
        esp_rom_delay_us(60);
        release_line(pin);
        esp_rom_delay_us(10);
    }
}

static bool one_wire_read_bit(gpio_num_t pin)
{
    drive_low(pin);
    esp_rom_delay_us(6);
    release_line(pin);
    esp_rom_delay_us(9);
    bool bit = read_line(pin);
    esp_rom_delay_us(55);
    return bit;
}

static void one_wire_write_byte(gpio_num_t pin, uint8_t value)
{
    for (uint8_t bit = 0; bit < 8; ++bit) {
        one_wire_write_bit(pin, (value & (1u << bit)) != 0);
    }
}

static uint8_t one_wire_read_byte(gpio_num_t pin)
{
    uint8_t value = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        if (one_wire_read_bit(pin)) {
            value |= (uint8_t)(1u << bit);
        }
    }
    return value;
}

static uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        uint8_t in = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mix = (uint8_t)((crc ^ in) & 1u);
            crc >>= 1;
            if (mix != 0) {
                crc ^= 0x8Cu;
            }
            in >>= 1;
        }
    }
    return crc;
}

esp_err_t b2_onewire_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "create 1-Wire lock");

    const b2_board_config_t *cfg = b2_config_get();
    for (size_t i = 0; i < B2_ONEWIRE_COUNT; ++i) {
        if (cfg->one_wire[i] < 0) {
            continue;
        }
        release_line(cfg->one_wire[i]);
    }
    s_initialized = true;
    ESP_LOGI(TAG, "initialized %u independent 1-Wire channels", B2_ONEWIRE_COUNT);
    return ESP_OK;
}

esp_err_t b2_onewire_probe(uint8_t channel, bool *present)
{
    ESP_RETURN_ON_ERROR(validate_channel(channel), TAG, "validate channel");
    ESP_RETURN_ON_FALSE(present != NULL, ESP_ERR_INVALID_ARG, TAG, "null presence output");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "1-Wire not initialized");

    const gpio_num_t pin = b2_config_get()->one_wire[channel];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *present = one_wire_reset(pin);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t b2_onewire_read_rom(uint8_t channel, uint64_t *rom_code)
{
    ESP_RETURN_ON_ERROR(validate_channel(channel), TAG, "validate channel");
    ESP_RETURN_ON_FALSE(rom_code != NULL, ESP_ERR_INVALID_ARG, TAG, "null ROM output");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "1-Wire not initialized");

    const gpio_num_t pin = b2_config_get()->one_wire[channel];
    uint8_t rom[8] = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool present = one_wire_reset(pin);
    if (present) {
        one_wire_write_byte(pin, 0x33); // READ ROM; valid for a single-drop bus.
        for (size_t i = 0; i < sizeof(rom); ++i) {
            rom[i] = one_wire_read_byte(pin);
        }
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_FALSE(present, ESP_ERR_NOT_FOUND, TAG, "no 1-Wire device");
    ESP_RETURN_ON_FALSE(crc8(rom, 7) == rom[7], ESP_ERR_INVALID_CRC, TAG, "ROM CRC mismatch");

    *rom_code = 0;
    for (size_t i = 0; i < 8; ++i) {
        *rom_code |= ((uint64_t)rom[i]) << (8u * i);
    }
    return ESP_OK;
}

esp_err_t b2_onewire_read_celsius(uint8_t channel, float *temperature_c)
{
    ESP_RETURN_ON_ERROR(validate_channel(channel), TAG, "validate channel");
    ESP_RETURN_ON_FALSE(temperature_c != NULL, ESP_ERR_INVALID_ARG, TAG, "null temperature output");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "1-Wire not initialized");

    const gpio_num_t pin = b2_config_get()->one_wire[channel];
    uint8_t scratchpad[9] = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool present = one_wire_reset(pin);
    if (present) {
        one_wire_write_byte(pin, 0xCC); // SKIP ROM; one device per configured channel.
        one_wire_write_byte(pin, 0x44); // CONVERT T.
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_FALSE(present, ESP_ERR_NOT_FOUND, TAG, "no 1-Wire device");

    // 12-bit DS18B20 conversion maximum. The bus is externally powered;
    // parasite-power strong pull-up is intentionally not assumed.
    vTaskDelay(pdMS_TO_TICKS(750));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    present = one_wire_reset(pin);
    if (present) {
        one_wire_write_byte(pin, 0xCC); // SKIP ROM.
        one_wire_write_byte(pin, 0xBE); // READ SCRATCHPAD.
        for (size_t i = 0; i < sizeof(scratchpad); ++i) {
            scratchpad[i] = one_wire_read_byte(pin);
        }
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_FALSE(present, ESP_ERR_NOT_FOUND, TAG, "device disappeared during read");
    ESP_RETURN_ON_FALSE(crc8(scratchpad, 8) == scratchpad[8], ESP_ERR_INVALID_CRC, TAG, "scratchpad CRC mismatch");

    int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    *temperature_c = (float)raw / 16.0f;
    return ESP_OK;
}
