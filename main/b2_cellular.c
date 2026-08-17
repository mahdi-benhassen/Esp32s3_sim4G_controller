#include "b2_cellular.h"

#include "b2_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_modem_config.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if CONFIG_B2_CELLULAR_PPP_ENABLED
static const char *TAG = "b2_cellular";
static esp_modem_dce_t *s_dce;
static esp_netif_t *s_netif;
#endif
static b2_cellular_status_t s_status;

#if CONFIG_B2_CELLULAR_PPP_ENABLED
static void copy_ip(void)
{
    if (s_netif == NULL) {
        s_status.ip[0] = '\0';
        return;
    }
    esp_netif_ip_info_t info = {0};
    if (esp_netif_get_ip_info(s_netif, &info) == ESP_OK) {
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&info.ip));
    } else {
        s_status.ip[0] = '\0';
    }
}

static void cellular_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_PPP_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_netif = event->esp_netif;
        s_status.connected = true;
        s_status.retrying = false;
        copy_ip();
        ESP_LOGI(TAG, "PPP netif up: %s", s_status.ip);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_PPP_LOST_IP) {
        s_status.connected = false;
        s_status.retrying = true;
        s_status.ip[0] = '\0';
        ESP_LOGW(TAG, "PPP netif lost; esp_modem will retry at the modem layer");
    } else if (event_base == NETIF_PPP_STATUS) {
        ESP_LOGD(TAG, "PPP status event: %" PRId32, event_id);
    }
}

static void cellular_retry_task(void *arg)
{
    (void)arg;
    TickType_t delay = pdMS_TO_TICKS(5000);
    for (;;) {
        if (s_dce != NULL && !s_status.connected) {
            s_status.retrying = true;
            s_status.retry_count++;
            const esp_err_t err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "PPP retry %" PRIu32 " failed: %s", s_status.retry_count, esp_err_to_name(err));
            }
            vTaskDelay(delay);
            if (delay < pdMS_TO_TICKS(60000)) {
                delay *= 2;
                if (delay > pdMS_TO_TICKS(60000)) {
                    delay = pdMS_TO_TICKS(60000);
                }
            }
        } else {
            delay = pdMS_TO_TICKS(5000);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

static esp_err_t configure_pdp_auth(const b2_settings_t *settings)
{
    if (settings->apn_auth_type == B2_APN_AUTH_NONE) {
        return ESP_OK;
    }
    char command[256] = {0};
    const int auth = settings->apn_auth_type == B2_APN_AUTH_CHAP ? 2 : 1;
    const int length = snprintf(command, sizeof(command), "AT+CGAUTH=1,%d,\"%s\",\"%s\"", auth,
                                settings->apn_username, settings->apn_password);
    if (length <= 0 || length >= (int)sizeof(command)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_modem_command(s_dce, command, NULL, 3000);
}
#endif

esp_err_t b2_cellular_start(const b2_settings_t *settings)
{
#if !CONFIG_B2_CELLULAR_PPP_ENABLED
    (void)settings;
    memset(&s_status, 0, sizeof(s_status));
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (settings == NULL || settings->apn[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const b2_board_config_t *cfg = b2_config_get();
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_PPP();
    s_netif = esp_netif_new(&netif_config);
    if (s_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(settings->apn);
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = cfg->modem_uart;
    dte_config.uart_config.tx_io_num = cfg->modem_tx;
    dte_config.uart_config.rx_io_num = cfg->modem_rx;
    dte_config.uart_config.rts_io_num = cfg->modem_rts;
    dte_config.uart_config.cts_io_num = cfg->modem_cts;
    dte_config.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, s_netif);
    if (s_dce == NULL) {
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, cellular_event, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, cellular_event, NULL);
    }
    if (err == ESP_OK) {
        err = configure_pdp_auth(settings);
    }
    if (err == ESP_OK) {
        err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PPP startup failed: %s", esp_err_to_name(err));
        esp_modem_destroy(s_dce);
        s_dce = NULL;
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return err;
    }
    s_status.enabled = true;
    s_status.started = true;
    s_status.retrying = false;
    if (xTaskCreate(cellular_retry_task, "b2_ppp_retry", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "PPP retry task could not be created; netif remains event-driven");
    }
    return ESP_OK;
#endif
}

esp_err_t b2_cellular_get_status(b2_cellular_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_status;
    return ESP_OK;
}
