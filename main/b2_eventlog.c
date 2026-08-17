#include "b2_eventlog.h"

#include "b2_storage.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_eventlog";
static const char *NVS_NAMESPACE = "b2log";
static const char *NVS_KEY = "ring";
static const uint32_t FLUSH_DIRTY_THRESHOLD = 4U;
static const int64_t FLUSH_INTERVAL_US = 10LL * 1000LL * 1000LL;

typedef struct {
    uint32_t next;
    uint32_t count;
    b2_event_t entries[B2_EVENT_LOG_CAPACITY];
} event_ring_t;

static event_ring_t s_ring;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_flush_task;
static bool s_ready;
static uint32_t s_dirty_count;
static int64_t s_last_append_us;
static int64_t s_last_flush_us;

static esp_err_t persist_locked(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open event log namespace");
    esp_err_t err = nvs_set_blob(handle, NVS_KEY, &s_ring, sizeof(s_ring));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void flush_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_ready || s_lock == NULL) {
            continue;
        }
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        const int64_t now_us = esp_timer_get_time();
        const bool due = s_dirty_count > 0U &&
                         (s_dirty_count >= FLUSH_DIRTY_THRESHOLD ||
                          now_us - s_last_append_us >= FLUSH_INTERVAL_US);
        if (due) {
            const esp_err_t err = persist_locked();
            if (err == ESP_OK) {
                s_dirty_count = 0;
                s_last_flush_us = now_us;
            } else {
                ESP_LOGW(TAG, "deferred event log flush failed: %s", esp_err_to_name(err));
            }
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t b2_event_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "create event log lock");
    memset(&s_ring, 0, sizeof(s_ring));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t length = sizeof(s_ring);
        err = nvs_get_blob(handle, NVS_KEY, &s_ring, &length);
        nvs_close(handle);
        if (err != ESP_OK || length != sizeof(s_ring) || s_ring.next >= B2_EVENT_LOG_CAPACITY || s_ring.count > B2_EVENT_LOG_CAPACITY) {
            memset(&s_ring, 0, sizeof(s_ring));
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    s_ready = err == ESP_OK;
    if (!s_ready) {
        return err;
    }
    s_dirty_count = 0;
    s_last_append_us = esp_timer_get_time();
    s_last_flush_us = s_last_append_us;
    if (xTaskCreate(flush_task, "b2_evt_flush", 3072, NULL, 2, &s_flush_task) != pdPASS) {
        s_ready = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t b2_event_log_append(b2_event_type_t type, uint8_t source, int32_t value, const char *text)
{
    ESP_RETURN_ON_FALSE(s_ready && s_lock != NULL, ESP_ERR_INVALID_STATE, TAG, "event log not initialized");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    b2_event_t *event = &s_ring.entries[s_ring.next];
    memset(event, 0, sizeof(*event));
    event->sequence = s_ring.count > 0 ? s_ring.entries[(s_ring.next + B2_EVENT_LOG_CAPACITY - 1U) % B2_EVENT_LOG_CAPACITY].sequence + 1U : 1U;
    event->timestamp_us = esp_timer_get_time();
    event->type = (uint8_t)type;
    event->source = source;
    event->value = value;
    if (text != NULL) {
        snprintf(event->text, sizeof(event->text), "%s", text);
    }
    s_ring.next = (s_ring.next + 1U) % B2_EVENT_LOG_CAPACITY;
    if (s_ring.count < B2_EVENT_LOG_CAPACITY) {
        s_ring.count++;
    }
    s_dirty_count++;
    s_last_append_us = event->timestamp_us;

    char line[160] = {0};
    snprintf(line, sizeof(line), "{\"seq\":%lu,\"ts_us\":%lld,\"type\":%u,\"source\":%u,\"value\":%ld,\"text\":\"%s\"}\n",
             (unsigned long)event->sequence, (long long)event->timestamp_us, event->type, event->source,
             (long)event->value, event->text);
    if (b2_storage_is_mounted() && b2_storage_append_text("events.jsonl", line) != ESP_OK) {
        ESP_LOGW(TAG, "SD event mirror append failed");
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t b2_event_log_flush(void)
{
    ESP_RETURN_ON_FALSE(s_ready && s_lock != NULL, ESP_ERR_INVALID_STATE, TAG, "event log not initialized");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_dirty_count > 0U) {
        err = persist_locked();
        if (err == ESP_OK) {
            s_dirty_count = 0;
            s_last_flush_us = esp_timer_get_time();
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t b2_event_log_get_newest(uint8_t index, b2_event_t *event)
{
    ESP_RETURN_ON_FALSE(event != NULL && s_ready && s_lock != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid event read");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (index >= s_ring.count) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    const uint32_t newest = (s_ring.next + B2_EVENT_LOG_CAPACITY - 1U) % B2_EVENT_LOG_CAPACITY;
    const uint32_t position = (newest + B2_EVENT_LOG_CAPACITY - index) % B2_EVENT_LOG_CAPACITY;
    *event = s_ring.entries[position];
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

uint32_t b2_event_log_count(void)
{
    return s_ring.count;
}
