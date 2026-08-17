#include "b2_oled.h"

#include "b2_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define SSD1306_ADDRESS 0x3C
#define SSD1306_WIDTH 128
#define SSD1306_PAGES 8

static esp_err_t oled_write(const uint8_t *data, size_t length)
{
    const b2_board_config_t *cfg = b2_config_get();
    return i2c_master_write_to_device(cfg->i2c_port, SSD1306_ADDRESS, data, length, pdMS_TO_TICKS(100));
}

static esp_err_t oled_command(uint8_t command)
{
    const uint8_t data[2] = {0x00, command};
    return oled_write(data, sizeof(data));
}

static void glyph(char c, uint8_t out[5])
{
    memset(out, 0, 5);
    switch (c) {
        case '0': memcpy(out, (uint8_t[]){0x3E,0x51,0x49,0x45,0x3E}, 5); break;
        case '1': memcpy(out, (uint8_t[]){0x00,0x42,0x7F,0x40,0x00}, 5); break;
        case '2': memcpy(out, (uint8_t[]){0x42,0x61,0x51,0x49,0x46}, 5); break;
        case '3': memcpy(out, (uint8_t[]){0x21,0x41,0x45,0x4B,0x31}, 5); break;
        case '4': memcpy(out, (uint8_t[]){0x18,0x14,0x12,0x7F,0x10}, 5); break;
        case '5': memcpy(out, (uint8_t[]){0x27,0x45,0x45,0x45,0x39}, 5); break;
        case '6': memcpy(out, (uint8_t[]){0x3C,0x4A,0x49,0x49,0x30}, 5); break;
        case '7': memcpy(out, (uint8_t[]){0x01,0x71,0x09,0x05,0x03}, 5); break;
        case '8': memcpy(out, (uint8_t[]){0x36,0x49,0x49,0x49,0x36}, 5); break;
        case '9': memcpy(out, (uint8_t[]){0x06,0x49,0x49,0x29,0x1E}, 5); break;
        case 'A': memcpy(out, (uint8_t[]){0x7E,0x11,0x11,0x11,0x7E}, 5); break;
        case 'B': memcpy(out, (uint8_t[]){0x7F,0x49,0x49,0x49,0x36}, 5); break;
        case 'C': memcpy(out, (uint8_t[]){0x3E,0x41,0x41,0x41,0x22}, 5); break;
        case 'D': memcpy(out, (uint8_t[]){0x7F,0x41,0x41,0x22,0x1C}, 5); break;
        case 'E': memcpy(out, (uint8_t[]){0x7F,0x49,0x49,0x49,0x41}, 5); break;
        case 'F': memcpy(out, (uint8_t[]){0x7F,0x09,0x09,0x09,0x01}, 5); break;
        case 'G': memcpy(out, (uint8_t[]){0x3E,0x41,0x49,0x49,0x7A}, 5); break;
        case 'H': memcpy(out, (uint8_t[]){0x7F,0x08,0x08,0x08,0x7F}, 5); break;
        case 'I': memcpy(out, (uint8_t[]){0x00,0x41,0x7F,0x41,0x00}, 5); break;
        case 'L': memcpy(out, (uint8_t[]){0x7F,0x40,0x40,0x40,0x40}, 5); break;
        case 'M': memcpy(out, (uint8_t[]){0x7F,0x02,0x0C,0x02,0x7F}, 5); break;
        case 'N': memcpy(out, (uint8_t[]){0x7F,0x04,0x08,0x10,0x7F}, 5); break;
        case 'O': memcpy(out, (uint8_t[]){0x3E,0x41,0x41,0x41,0x3E}, 5); break;
        case 'P': memcpy(out, (uint8_t[]){0x7F,0x09,0x09,0x09,0x06}, 5); break;
        case 'R': memcpy(out, (uint8_t[]){0x7F,0x09,0x19,0x29,0x46}, 5); break;
        case 'S': memcpy(out, (uint8_t[]){0x46,0x49,0x49,0x49,0x31}, 5); break;
        case 'T': memcpy(out, (uint8_t[]){0x01,0x01,0x7F,0x01,0x01}, 5); break;
        case 'U': memcpy(out, (uint8_t[]){0x3F,0x40,0x40,0x40,0x3F}, 5); break;
        case 'W': memcpy(out, (uint8_t[]){0x7F,0x20,0x18,0x20,0x7F}, 5); break;
        case 'Y': memcpy(out, (uint8_t[]){0x07,0x08,0x70,0x08,0x07}, 5); break;
        case ':': memcpy(out, (uint8_t[]){0x00,0x36,0x36,0x00,0x00}, 5); break;
        case '.': memcpy(out, (uint8_t[]){0x00,0x60,0x60,0x00,0x00}, 5); break;
        case '-': memcpy(out, (uint8_t[]){0x08,0x08,0x08,0x08,0x08}, 5); break;
        case '/': memcpy(out, (uint8_t[]){0x20,0x10,0x08,0x04,0x02}, 5); break;
        default: break;
    }
}

static esp_err_t oled_write_line(uint8_t page, const char *text)
{
    uint8_t data[SSD1306_WIDTH + 1] = {0};
    data[0] = 0x40;
    for (size_t i = 0; text != NULL && text[i] != '\0' && i < 21; ++i) {
        uint8_t g[5];
        glyph(text[i], g);
        memcpy(&data[1 + i * 6], g, 5);
    }
    ESP_RETURN_ON_ERROR(oled_command((uint8_t)(0xB0 | page)), "b2_oled", "set page");
    ESP_RETURN_ON_ERROR(oled_command(0x00), "b2_oled", "set low column");
    ESP_RETURN_ON_ERROR(oled_command(0x10), "b2_oled", "set high column");
    return oled_write(data, sizeof(data));
}

esp_err_t b2_oled_init(void)
{
    static const uint8_t init[] = {
        0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81,
        0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    return oled_write(init, sizeof(init));
}

esp_err_t b2_oled_show_status(const char *line1, const char *line2, const char *line3, const char *line4)
{
    const char *lines[4] = {line1, line2, line3, line4};
    for (uint8_t page = 0; page < 4; ++page) {
        ESP_RETURN_ON_ERROR(oled_write_line(page, lines[page]), "b2_oled", "write status line");
    }
    return ESP_OK;
}
