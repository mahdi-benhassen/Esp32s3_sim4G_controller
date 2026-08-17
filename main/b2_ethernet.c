#include "b2_ethernet.h"

#include "b2_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include <string.h>

#if CONFIG_B2_ETHERNET_ENABLED
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "driver/spi_master.h"

static const char *TAG = "b2_ethernet";
#endif
static bool s_started;
#if CONFIG_B2_ETHERNET_ENABLED
static esp_eth_handle_t s_eth_handle;
static esp_netif_t *s_netif;
static esp_eth_netif_glue_handle_t s_glue;
#endif

esp_err_t b2_ethernet_start(void)
{
#if !CONFIG_B2_ETHERNET_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_started) {
        return ESP_OK;
    }
    const b2_board_config_t *board = b2_config_get();
    ESP_RETURN_ON_ERROR(b2_config_validate(board), TAG, "invalid board profile");
    ESP_RETURN_ON_FALSE(board->ethernet_mosi >= 0 && board->ethernet_miso >= 0 && board->ethernet_sclk >= 0 &&
                        board->ethernet_cs >= 0, ESP_ERR_INVALID_ARG, TAG, "Ethernet pins are not provisioned");

    spi_bus_config_t bus_config = {
        .mosi_io_num = board->ethernet_mosi,
        .miso_io_num = board->ethernet_miso,
        .sclk_io_num = board->ethernet_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1536,
    };
    esp_err_t err = spi_bus_initialize(board->ethernet_spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t device_config = {
        .command_bits = 0,
        .address_bits = 0,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = board->ethernet_cs,
        .queue_size = 20,
    };
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(board->ethernet_spi_host, &device_config);
    w5500_config.int_gpio_num = board->ethernet_irq;
    w5500_config.poll_period_ms = board->ethernet_irq >= 0 ? 0 : 20;
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    ESP_RETURN_ON_FALSE(mac != NULL && phy != NULL, ESP_ERR_NO_MEM, TAG, "create W5500 MAC/PHY");
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &s_eth_handle), TAG, "install Ethernet driver");

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&netif_config);
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_ERR_NO_MEM, TAG, "create Ethernet netif");
    s_glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_FALSE(s_glue != NULL, ESP_ERR_NO_MEM, TAG, "create Ethernet netif glue");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_netif, s_glue), TAG, "attach Ethernet netif");
    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth_handle), TAG, "start Ethernet driver");
    s_started = true;
    ESP_LOGI(TAG, "native W5500 Ethernet started");
    return ESP_OK;
#endif
}

bool b2_ethernet_is_started(void)
{
    return s_started;
}
