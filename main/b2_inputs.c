#include "b2_inputs.h"

#include "b2_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "b2_inputs";
static bool s_active[B2_DRY_INPUT_COUNT];
static b2_input_callback_t s_callback;
static void *s_context;

static bool read_active(uint8_t channel)
{
    const b2_board_config_t *cfg = b2_config_get();
    const int level = gpio_get_level(cfg->dry_input[channel]);
    return cfg->dry_input_active_low ? (level == 0) : (level != 0);
}

static void input_task(void *arg)
{
    (void)arg;
    bool candidate[B2_DRY_INPUT_COUNT] = {false, false};
    int64_t candidate_since[B2_DRY_INPUT_COUNT] = {0, 0};
    const int64_t debounce_us = 50000;

    for (;;) {
        const int64_t now = esp_timer_get_time();
        for (uint8_t i = 0; i < B2_DRY_INPUT_COUNT; ++i) {
            const bool sample = read_active(i);
            if (sample != candidate[i]) {
                candidate[i] = sample;
                candidate_since[i] = now;
            } else if (sample != s_active[i] && (now - candidate_since[i]) >= debounce_us) {
                s_active[i] = sample;
                ESP_LOGI(TAG, "input %u changed: %s", i + 1, sample ? "active" : "inactive");
                if (s_callback != NULL) {
                    s_callback(i, sample, s_context);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t b2_inputs_start(b2_input_callback_t callback, void *context)
{
    s_callback = callback;
    s_context = context;
    for (uint8_t i = 0; i < B2_DRY_INPUT_COUNT; ++i) {
        s_active[i] = read_active(i);
    }
    return xTaskCreate(input_task, "b2_inputs", 3072, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool b2_input_is_active(uint8_t channel)
{
    return channel < B2_DRY_INPUT_COUNT ? s_active[channel] : false;
}
