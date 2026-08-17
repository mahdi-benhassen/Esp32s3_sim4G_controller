#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define B2_STORAGE_MOUNT_POINT "/sdcard"

/** Mount the configured SPI SD card. A missing card is a non-fatal condition. */
esp_err_t b2_storage_init(void);

bool b2_storage_is_mounted(void);

/** Return a short card label/description for diagnostics. */
esp_err_t b2_storage_get_card_name(char *buffer, size_t buffer_size);

/** Write a text file below /sdcard, rejecting traversal and absolute paths. */
esp_err_t b2_storage_write_text(const char *relative_path, const char *text);

/** Append text to a file below /sdcard, creating it when absent. */
esp_err_t b2_storage_append_text(const char *relative_path, const char *text);

/** Read a text file below /sdcard into a caller-provided buffer. */
esp_err_t b2_storage_read_text(const char *relative_path, char *buffer, size_t buffer_size);

/** Delete a file below /sdcard after validating its relative path. */
esp_err_t b2_storage_delete_text(const char *relative_path);

/** Unmount the card and release the SPI device. */
esp_err_t b2_storage_unmount(void);

#ifdef __cplusplus
}
#endif
