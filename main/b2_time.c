#include "b2_time.h"

#include "b2_eventlog.h"
#include "b2_rtc.h"
#include "b2_settings.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "b2_time";
static TaskHandle_t s_task;
static bool s_started;
static bool s_synchronized;
static char s_timezone[B2_SETTINGS_TIMEZONE_MAX];

static void time_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t now = time(NULL);
            struct tm utc = {0};
            gmtime_r(&now, &utc);
            if (b2_rtc_set_time(&utc) == ESP_OK) {
                if (!s_synchronized) {
                    b2_event_log_append(B2_EVENT_NETWORK, 0, 1, "time_synchronized");
                }
                s_synchronized = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

esp_err_t b2_time_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    b2_settings_t settings = {0};
    if (b2_settings_load(&settings) != ESP_OK || !settings.time_sync_enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(s_timezone, sizeof(s_timezone), "%s", settings.timezone[0] != '\0' ? settings.timezone : "UTC0");
    setenv("TZ", s_timezone, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    s_started = true;
    if (xTaskCreate(time_task, "b2_time", 3072, NULL, 2, &s_task) != pdPASS) {
        esp_sntp_stop();
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "SNTP synchronization started with TZ=%s", s_timezone);
    return ESP_OK;
}

esp_err_t b2_time_get_status(b2_time_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    status->started = s_started;
    status->synchronized = s_synchronized;
    snprintf(status->timezone, sizeof(status->timezone), "%s", s_timezone);
    return ESP_OK;
}
