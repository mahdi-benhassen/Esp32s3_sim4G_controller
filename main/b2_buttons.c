#include "b2_buttons.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "b2_buttons";
static b2_button_callback_t s_callback;
static void *s_context;
static bool s_state[B2_BUTTON_COUNT];
static bool s_started;

static gpio_num_t button_pin(b2_button_id_t button)
{
    const b2_board_config_t *cfg = b2_config_get();
    switch (button) {
    case B2_BUTTON_RESET:
        return cfg->button_reset;
    case B2_BUTTON_DOWNLOAD:
        return cfg->button_download;
    case B2_BUTTON_CONFIG:
        return cfg->button_config;
    default:
        return GPIO_NUM_NC;
    }
}

static bool read_pressed(gpio_num_t pin)
{
    if (pin < 0) {
        return false;
    }
    const b2_board_config_t *cfg = b2_config_get();
    bool level_high = gpio_get_level(pin) != 0;
    return cfg->button_active_low ? !level_high : level_high;
}

static void button_task(void *arg)
{
    (void)arg;
    bool candidate[B2_BUTTON_COUNT] = {false};
    uint8_t stable_count[B2_BUTTON_COUNT] = {0};
    for (;;) {
        for (b2_button_id_t button = B2_BUTTON_RESET; button < B2_BUTTON_COUNT; ++button) {
            gpio_num_t pin = button_pin(button);
            bool pressed = read_pressed(pin);
            if (pressed != candidate[button]) {
                candidate[button] = pressed;
                stable_count[button] = 0;
            } else if (stable_count[button] < 3) {
                ++stable_count[button];
            }
            if (stable_count[button] >= 3 && s_state[button] != candidate[button]) {
                s_state[button] = candidate[button];
                if (s_callback != NULL) {
                    s_callback(button, s_state[button], s_context);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t b2_buttons_start(b2_button_callback_t callback, void *context)
{
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "buttons already started");
    s_callback = callback;
    s_context = context;
    const b2_board_config_t *cfg = b2_config_get();
    for (b2_button_id_t button = B2_BUTTON_RESET; button < B2_BUTTON_COUNT; ++button) {
        gpio_num_t pin = button_pin(button);
        if (pin < 0) {
            continue;
        }
        ESP_RETURN_ON_ERROR(gpio_set_direction(pin, GPIO_MODE_INPUT), TAG, "configure button GPIO");
        ESP_RETURN_ON_ERROR(gpio_set_pull_mode(pin, cfg->button_active_low ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY),
                            TAG, "configure button pull");
        s_state[button] = read_pressed(pin);
    }
    s_started = true;
    BaseType_t created = xTaskCreate(button_task, "b2_buttons", 3072, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "create button task");
    ESP_LOGI(TAG, "button polling started");
    return ESP_OK;
}

bool b2_button_is_pressed(b2_button_id_t button)
{
    if (button >= B2_BUTTON_COUNT) {
        return false;
    }
    return s_state[button];
}
