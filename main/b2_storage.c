#include "b2_storage.h"

#include "b2_config.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_storage";
static sdmmc_card_t *s_card;
static bool s_mounted;

static bool safe_relative_path(const char *relative_path)
{
    return relative_path != NULL && relative_path[0] != '\0' && relative_path[0] != '/' &&
           strstr(relative_path, "..") == NULL && strchr(relative_path, '\\') == NULL;
}

static esp_err_t make_path(const char *relative_path, char *path, size_t path_size)
{
    ESP_RETURN_ON_FALSE(safe_relative_path(relative_path), ESP_ERR_INVALID_ARG, TAG, "unsafe storage path");
    int written = snprintf(path, path_size, "%s/%s", B2_STORAGE_MOUNT_POINT, relative_path);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < path_size, ESP_ERR_INVALID_SIZE, TAG, "storage path too long");
    return ESP_OK;
}

esp_err_t b2_storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    const b2_board_config_t *cfg = b2_config_get();
    ESP_RETURN_ON_FALSE(cfg->sd_cs >= 0 && cfg->sd_mosi >= 0 && cfg->sd_miso >= 0 && cfg->sd_sclk >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "SD SPI pins are not configured");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = cfg->sd_spi_host;
    spi_bus_config_t bus_config = {
        .mosi_io_num = cfg->sd_mosi,
        .miso_io_num = cfg->sd_miso,
        .sclk_io_num = cfg->sd_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    esp_err_t err = spi_bus_initialize(cfg->sd_spi_host, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD SPI bus unavailable: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = cfg->sd_cs;
    slot_config.host_id = cfg->sd_spi_host;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdspi_mount(B2_STORAGE_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(err));
        spi_bus_free(cfg->sd_spi_host);
        s_card = NULL;
        return err;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s", B2_STORAGE_MOUNT_POINT);
    return ESP_OK;
}

bool b2_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t b2_storage_get_card_name(char *buffer, size_t buffer_size)
{
    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_size > 0, ESP_ERR_INVALID_ARG, TAG, "invalid card name buffer");
    ESP_RETURN_ON_FALSE(s_mounted && s_card != NULL, ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");
    int written = snprintf(buffer, buffer_size, "%s", s_card->cid.name);
    ESP_RETURN_ON_FALSE(written >= 0 && (size_t)written < buffer_size, ESP_ERR_INVALID_SIZE, TAG, "card name buffer too small");
    return ESP_OK;
}

esp_err_t b2_storage_write_text(const char *relative_path, const char *text)
{
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "null text");
    ESP_RETURN_ON_FALSE(s_mounted, ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");
    char path[128] = {0};
    ESP_RETURN_ON_ERROR(make_path(relative_path, path, sizeof(path)), TAG, "make storage path");
    FILE *file = fopen(path, "w");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_FAIL, TAG, "open storage file for write");
    size_t length = strlen(text);
    size_t written = fwrite(text, 1, length, file);
    int close_result = fclose(file);
    ESP_RETURN_ON_FALSE(written == length && close_result == 0, ESP_FAIL, TAG, "write storage file");
    return ESP_OK;
}

esp_err_t b2_storage_read_text(const char *relative_path, char *buffer, size_t buffer_size)
{
    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_size > 0, ESP_ERR_INVALID_ARG, TAG, "invalid read buffer");
    ESP_RETURN_ON_FALSE(s_mounted, ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");
    char path[128] = {0};
    ESP_RETURN_ON_ERROR(make_path(relative_path, path, sizeof(path)), TAG, "make storage path");
    FILE *file = fopen(path, "r");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_ERR_NOT_FOUND, TAG, "open storage file for read");
    size_t read_size = fread(buffer, 1, buffer_size - 1, file);
    int close_result = fclose(file);
    buffer[read_size] = '\0';
    ESP_RETURN_ON_FALSE(close_result == 0, ESP_FAIL, TAG, "close storage file");
    return ESP_OK;
}

esp_err_t b2_storage_unmount(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    const b2_board_config_t *cfg = b2_config_get();
    esp_err_t err = esp_vfs_fat_sdcard_unmount(B2_STORAGE_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    esp_err_t bus_err = spi_bus_free(cfg->sd_spi_host);
    if (err != ESP_OK) {
        return err;
    }
    return bus_err;
}
