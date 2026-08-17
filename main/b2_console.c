#include "b2_adc.h"
#include "b2_buttons.h"
#include "b2_eventlog.h"
#include "b2_inputs.h"
#include "b2_http.h"
#include "b2_modem.h"
#include "b2_mqtt.h"
#include "b2_modbus.h"
#include "b2_onewire.h"
#include "b2_relay.h"
#include "b2_storage.h"
#include "b2_ota.h"
#include "b2_time.h"
#include "b2_settings.h"
#include "b2_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "b2_console";

static void print_help(void)
{
    printf("Commands: relay <1|2> <on|off|toggle>, input, adc <1..4>, cal <1..4> <gain> <offset>, rule <index> ..., rules, onewire <1..4>, modbus read/write, wifi [set/off], mqtt [set/off], modem [gnss|apn|pdp], http, time [server <hostname>], events, ota status, storage, button, help\n");
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96] = {0};
    print_help();
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            clearerr(stdin);
            continue;
        }
        unsigned channel = 0;
        char action[16] = {0};
        if (sscanf(line, "relay %u %15s", &channel, action) == 2 && channel >= 1 && channel <= 2) {
            if (strcmp(action, "on") == 0) {
                b2_relay_set((uint8_t)(channel - 1), true);
            } else if (strcmp(action, "off") == 0) {
                b2_relay_set((uint8_t)(channel - 1), false);
            } else if (strcmp(action, "toggle") == 0) {
                b2_relay_toggle((uint8_t)(channel - 1));
            } else {
                printf("Unknown relay action\n");
            }
            continue;
        }
        if (strncmp(line, "input", 5) == 0) {
            printf("INPUT1=%s INPUT2=%s\n", b2_input_is_active(0) ? "ON" : "OFF", b2_input_is_active(1) ? "ON" : "OFF");
            continue;
        }
        float gain = 0.0f;
        float offset = 0.0f;
        if (sscanf(line, "cal %u %f %f", &channel, &gain, &offset) == 3 && channel >= 1 && channel <= 4 && gain > 0.0f && gain < 1000.0f) {
            b2_settings_t settings = {0};
            esp_err_t err = b2_settings_load(&settings);
            if (err == ESP_OK) {
                settings.analog_gain[channel - 1U] = gain;
                settings.analog_offset[channel - 1U] = offset;
                err = b2_settings_save(&settings);
            }
            printf("CAL channel=%u %s; reboot to apply\n", channel, err == ESP_OK ? "saved" : esp_err_to_name(err));
            continue;
        }
        if (sscanf(line, "adc %u", &channel) == 1 && channel >= 1 && channel <= 4) {
            float value = 0.0f;
            if (channel <= 2) {
                if (b2_adc_read_voltage((uint8_t)(channel - 1), &value) == ESP_OK) {
                    printf("ADC%u=%.3f V\n", channel, value);
                } else {
                    printf("ADC%u read failed\n", channel);
                }
            } else if (b2_adc_read_4_20ma((uint8_t)(channel - 1), &value) == ESP_OK) {
                printf("ADC%u=%.3f mA\n", channel, value);
            } else {
                printf("ADC%u read failed\n", channel);
            }
            continue;
        }
        if (sscanf(line, "onewire %u", &channel) == 1 && channel >= 1 && channel <= 4) {
            float temperature = 0.0f;
            uint64_t rom = 0;
            esp_err_t temp_err = b2_onewire_read_celsius((uint8_t)(channel - 1), &temperature);
            esp_err_t rom_err = b2_onewire_read_rom((uint8_t)(channel - 1), &rom);
            if (temp_err == ESP_OK) {
                printf("1WIRE%u=%.2f C\n", channel, temperature);
            } else {
                printf("1WIRE%u temperature read failed: %s\n", channel, esp_err_to_name(temp_err));
            }
            if (rom_err == ESP_OK) {
                printf("1WIRE%u ROM=0x%016" PRIx64 "\n", channel, rom);
            }
            continue;
        }
        unsigned rule_index = 0;
        unsigned rule_source = 0;
        unsigned rule_duration = 0;
        unsigned rule_target = 0;
        char rule_action[8] = {0};
        float rule_threshold = 0.0f;
        if (sscanf(line, "rule %u input %u %u relay %u %7s", &rule_index, &rule_source, &rule_duration, &rule_target, rule_action) == 5 &&
            rule_index >= 1 && rule_index <= B2_SETTINGS_RULE_COUNT && rule_source < 2 && rule_duration <= 86400000U && rule_target >= 1 && rule_target <= 2) {
            b2_settings_t settings = {0};
            esp_err_t err = b2_settings_load(&settings);
            if (err == ESP_OK) {
                b2_rule_t *rule = &settings.rules[rule_index - 1U];
                memset(rule, 0, sizeof(*rule));
                rule->enabled = true;
                rule->condition = B2_RULE_INPUT_ACTIVE;
                rule->source = (uint8_t)rule_source;
                rule->duration_ms = rule_duration;
                rule->action = strcasecmp(rule_action, "toggle") == 0 ? B2_RULE_ACTION_RELAY_TOGGLE : B2_RULE_ACTION_RELAY_SET;
                rule->target = (uint8_t)(rule_target - 1U);
                rule->action_state = strcasecmp(rule_action, "on") == 0;
                err = b2_settings_save(&settings);
            }
            printf("RULE %u %s; reboot to apply\n", rule_index, err == ESP_OK ? "saved" : esp_err_to_name(err));
            continue;
        }
        if (sscanf(line, "rule %u adc %u above %f relay %u %7s", &rule_index, &rule_source, &rule_threshold, &rule_target, rule_action) == 5 &&
            rule_index >= 1 && rule_index <= B2_SETTINGS_RULE_COUNT && rule_source < 4 && rule_target >= 1 && rule_target <= 2) {
            b2_settings_t settings = {0};
            esp_err_t err = b2_settings_load(&settings);
            if (err == ESP_OK) {
                b2_rule_t *rule = &settings.rules[rule_index - 1U];
                memset(rule, 0, sizeof(*rule));
                rule->enabled = true;
                rule->condition = B2_RULE_ADC_ABOVE;
                rule->source = (uint8_t)rule_source;
                rule->threshold = rule_threshold;
                rule->action = strcasecmp(rule_action, "toggle") == 0 ? B2_RULE_ACTION_RELAY_TOGGLE : B2_RULE_ACTION_RELAY_SET;
                rule->target = (uint8_t)(rule_target - 1U);
                rule->action_state = strcasecmp(rule_action, "on") == 0;
                err = b2_settings_save(&settings);
            }
            printf("RULE %u %s; reboot to apply\n", rule_index, err == ESP_OK ? "saved" : esp_err_to_name(err));
            continue;
        }
        if (sscanf(line, "rule %u disable", &rule_index) == 1 && rule_index >= 1 && rule_index <= B2_SETTINGS_RULE_COUNT) {
            b2_settings_t settings = {0};
            esp_err_t err = b2_settings_load(&settings);
            if (err == ESP_OK) {
                memset(&settings.rules[rule_index - 1U], 0, sizeof(settings.rules[rule_index - 1U]));
                err = b2_settings_save(&settings);
            }
            printf("RULE %u %s; reboot to apply\n", rule_index, err == ESP_OK ? "disabled" : esp_err_to_name(err));
            continue;
        }
        if (strncmp(line, "rules", 5) == 0) {
            b2_settings_t settings = {0};
            esp_err_t err = b2_settings_load(&settings);
            if (err == ESP_OK) {
                printf("RULES count=%u\n", settings.rule_count);
                for (uint8_t i = 0; i < settings.rule_count && i < B2_SETTINGS_RULE_COUNT; ++i) {
                    printf("RULE%u enabled=%s condition=%u source=%u action=%u target=%u duration_ms=%" PRIu32 " threshold=%.3f\n",
                           (unsigned)(i + 1U), settings.rules[i].enabled ? "yes" : "no", settings.rules[i].condition,
                           settings.rules[i].source, settings.rules[i].action, settings.rules[i].target,
                           settings.rules[i].duration_ms, settings.rules[i].threshold);
                }
            } else {
                printf("RULES read failed: %s\n", esp_err_to_name(err));
            }
            continue;
        }
        unsigned slave = 0;
        unsigned address = 0;
        unsigned count = 0;
        if (sscanf(line, "modbus read %u %u %u", &slave, &address, &count) == 3 &&
            slave <= 247 && address <= 65535 && count > 0 && count <= 8) {
            uint16_t registers[8] = {0};
            esp_err_t err = b2_modbus_read_holding_registers((uint8_t)slave, (uint16_t)address, (uint16_t)count,
                                                              registers, 8);
            if (err == ESP_OK) {
                printf("MODBUS slave=%u address=%u", slave, address);
                for (unsigned i = 0; i < count; ++i) {
                    printf(" R%u=0x%04X", address + i, registers[i]);
                }
                printf("\\n");
            } else {
                printf("MODBUS read failed: %s\\n", esp_err_to_name(err));
            }
            continue;
        }
        unsigned value = 0;
        if (sscanf(line, "modbus write %u %u %u", &slave, &address, &value) == 3 &&
            slave <= 247 && address <= 65535 && value <= 65535) {
            esp_err_t err = b2_modbus_write_single_register((uint8_t)slave, (uint16_t)address, (uint16_t)value);
            printf("MODBUS write %s\\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
            continue;
        }
        if (strncmp(line, "wifi", 4) == 0) {
            if (strncmp(line, "wifi set ", 9) == 0) {
                char ssid[33] = {0};
                char password[65] = {0};
                if (sscanf(line + 9, "%32s %64s", ssid, password) == 2) {
                    b2_settings_t settings = {0};
                    esp_err_t err = b2_settings_load(&settings);
                    if (err == ESP_OK) {
                        settings.wifi_enabled = true;
                        snprintf(settings.wifi_ssid, sizeof(settings.wifi_ssid), "%s", ssid);
                        snprintf(settings.wifi_password, sizeof(settings.wifi_password), "%s", password);
                        err = b2_settings_save(&settings);
                    }
                    printf("WIFI credentials %s; reboot to apply\\n", err == ESP_OK ? "saved" : esp_err_to_name(err));
                } else {
                    printf("Usage: wifi set <ssid> <password>\\n");
                }
            } else if (strcmp(line, "wifi off") == 0) {
                b2_settings_t settings = {0};
                esp_err_t err = b2_settings_load(&settings);
                if (err == ESP_OK) {
                    settings.wifi_enabled = false;
                    err = b2_settings_save(&settings);
                }
                printf("WIFI %s; reboot to apply\\n", err == ESP_OK ? "disabled" : esp_err_to_name(err));
            } else {
                b2_wifi_status_t wifi = {0};
                b2_wifi_get_status(&wifi);
                printf("WIFI started=%s connected=%s SSID=%s IP=%s RSSI=%d\\n",
                       wifi.started ? "yes" : "no", wifi.connected ? "yes" : "no",
                       wifi.ssid[0] ? wifi.ssid : "<none>", wifi.ip, wifi.rssi);
            }
            continue;
        }
        if (strncmp(line, "mqtt", 4) == 0) {
            if (strncmp(line, "mqtt set ", 9) == 0) {
                char uri[128] = {0};
                char username[65] = {0};
                char password[65] = {0};
                int fields = sscanf(line + 9, "%127s %64s %64s", uri, username, password);
                if (fields >= 1) {
                    b2_settings_t settings = {0};
                    esp_err_t err = b2_settings_load(&settings);
                    if (err == ESP_OK) {
                        settings.mqtt_enabled = true;
                        snprintf(settings.mqtt_uri, sizeof(settings.mqtt_uri), "%s", uri);
                        if (fields >= 2) {
                            snprintf(settings.mqtt_username, sizeof(settings.mqtt_username), "%s", username);
                        }
                        if (fields >= 3) {
                            snprintf(settings.mqtt_password, sizeof(settings.mqtt_password), "%s", password);
                        }
                        err = b2_settings_save(&settings);
                    }
                    printf("MQTT configuration %s; reboot to apply\\n", err == ESP_OK ? "saved" : esp_err_to_name(err));
                } else {
                    printf("Usage: mqtt set <mqtt[s]://broker> [username] [password]\\n");
                }
            } else if (strcmp(line, "mqtt off") == 0) {
                b2_settings_t settings = {0};
                esp_err_t err = b2_settings_load(&settings);
                if (err == ESP_OK) {
                    settings.mqtt_enabled = false;
                    err = b2_settings_save(&settings);
                }
                printf("MQTT %s; reboot to apply\\n", err == ESP_OK ? "disabled" : esp_err_to_name(err));
            } else {
                b2_mqtt_status_t mqtt = {0};
                b2_mqtt_get_status(&mqtt);
                printf("MQTT started=%s connected=%s URI=%s topic=%s last_id=%d\\n",
                       mqtt.started ? "yes" : "no", mqtt.connected ? "yes" : "no",
                       mqtt.uri[0] ? mqtt.uri : "<none>", mqtt.base_topic, mqtt.last_message_id);
            }
            continue;
        }
        if (strncmp(line, "modem", 5) == 0) {
            if (strcmp(line, "modem gnss on\n") == 0) {
                printf("GNSS enable %s\n", b2_modem_gnss_enable(true) == ESP_OK ? "ok" : "failed");
            } else if (strcmp(line, "modem gnss off\n") == 0) {
                printf("GNSS disable %s\n", b2_modem_gnss_enable(false) == ESP_OK ? "ok" : "failed");
            } else if (strcmp(line, "modem gnss read\n") == 0) {
                b2_modem_gnss_t gnss = {0};
                esp_err_t err = b2_modem_gnss_get(&gnss);
                if (err == ESP_OK) {
                    printf("GNSS fix=%s satellites=%d utc=%s lat=%s lon=%s alt_m=%s speed_knots=%s\n",
                           gnss.fix_valid ? "yes" : "no", gnss.satellites, gnss.utc, gnss.latitude,
                           gnss.longitude, gnss.altitude_m, gnss.speed_knots);
                } else {
                    printf("GNSS read failed: %s\n", esp_err_to_name(err));
                }
            } else if (strncmp(line, "modem apn ", 10) == 0) {
                char apn[64] = {0};
                char username[65] = {0};
                char password[65] = {0};
                char auth[8] = {0};
                const int fields = sscanf(line + 10, "%63s %64s %64s %7s", apn, username, password, auth);
                if (fields >= 1) {
                    b2_apn_auth_type_t auth_type = B2_APN_AUTH_NONE;
                    if (fields >= 4 && strcasecmp(auth, "pap") == 0) {
                        auth_type = B2_APN_AUTH_PAP;
                    } else if (fields >= 4 && strcasecmp(auth, "chap") == 0) {
                        auth_type = B2_APN_AUTH_CHAP;
                    } else if (fields >= 4) {
                        printf("Usage: modem apn <apn> [username password pap|chap]\n");
                        continue;
                    }
                    b2_settings_t settings = {0};
                    esp_err_t err = b2_settings_load(&settings);
                    if (err == ESP_OK) {
                        snprintf(settings.apn, sizeof(settings.apn), "%s", apn);
                        settings.apn_auth_type = auth_type;
                        if (auth_type == B2_APN_AUTH_NONE) {
                            settings.apn_username[0] = '\0';
                            settings.apn_password[0] = '\0';
                        } else {
                            snprintf(settings.apn_username, sizeof(settings.apn_username), "%s", username);
                            snprintf(settings.apn_password, sizeof(settings.apn_password), "%s", password);
                        }
                        err = b2_settings_save(&settings);
                    }
                    if (err == ESP_OK) {
                        err = b2_modem_set_apn_auth(apn, username, password, auth_type);
                    }
                    printf("APN %s\n", err == ESP_OK ? "saved and applied" : esp_err_to_name(err));
                } else {
                    printf("Usage: modem apn <apn> [username password pap|chap]\n");
                }
            } else if (strcmp(line, "modem pdp\n") == 0) {
                b2_settings_t settings = {0};
                esp_err_t err = b2_settings_load(&settings);
                if (err == ESP_OK && settings.apn[0] != '\0') {
                    err = b2_modem_set_apn_auth(settings.apn, settings.apn_username, settings.apn_password, settings.apn_auth_type);
                }
                if (err == ESP_OK) {
                    err = b2_modem_activate_pdp();
                }
                printf("PDP activation %s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
            } else {
                b2_modem_status_t status = {0};
                b2_modem_get_status(&status);
                printf("MODEM registered=%s attached=%s CSQ=%d\n", status.registered ? "yes" : "no", status.packet_attached ? "yes" : "no", status.signal_quality);
                printf("Usage: modem gnss on|off|read; modem apn <apn> [username password pap|chap]; modem pdp\n");
            }
            continue;
        }
        if (strncmp(line, "http", 4) == 0) {
            b2_http_status_t http = {0};
            b2_http_get_status(&http);
            printf("HTTP started=%s tls=%s port=%u endpoints=/health,/api/v1/status,/api/v1/capabilities; relay writes=%s\n",
                   http.started ? "yes" : "no", http.tls ? "yes" : "no", http.port, http.tls ? "HTTPS-only" : "disabled");
            continue;
        }
        if (strncmp(line, "time server ", 12) == 0) {
            char server[B2_SETTINGS_SNTP_SERVER_MAX] = {0};
            if (sscanf(line + 12, "%127s", server) == 1) {
                b2_settings_t settings = {0};
                esp_err_t err = b2_settings_load(&settings);
                if (err == ESP_OK) {
                    snprintf(settings.sntp_server, sizeof(settings.sntp_server), "%s", server);
                    err = b2_settings_save(&settings);
                }
                printf("SNTP server %s; reboot to apply\\n", err == ESP_OK ? "saved" : esp_err_to_name(err));
            } else {
                printf("Usage: time server <hostname>\\n");
            }
            continue;
        }
        if (strncmp(line, "time", 4) == 0) {
            b2_time_status_t time_status = {0};
            b2_time_get_status(&time_status);
            printf("TIME started=%s synchronized=%s server=%s TZ=%s\\n", time_status.started ? "yes" : "no", time_status.synchronized ? "yes" : "no", time_status.server, time_status.timezone);
            continue;
        }
        if (strncmp(line, "events", 6) == 0) {
            printf("EVENTS count=%" PRIu32 "\n", b2_event_log_count());
            b2_event_t event = {0};
            if (b2_event_log_get_newest(0, &event) == ESP_OK) {
                printf("LATEST seq=%" PRIu32 " type=%u source=%u value=%" PRId32 " text=%s\n", event.sequence, event.type, event.source, event.value, event.text);
            }
            continue;
        }
        if (strncmp(line, "ota status", 10) == 0) {
            printf("OTA pending_verification=%s\n", b2_ota_is_pending_verify() ? "yes" : "no");
            continue;
        }
        if (strncmp(line, "storage", 7) == 0) {
            char card_name[16] = {0};
            if (b2_storage_get_card_name(card_name, sizeof(card_name)) == ESP_OK) {
                printf("SD mounted=yes card=%s\n", card_name);
            } else {
                printf("SD mounted=%s\n", b2_storage_is_mounted() ? "yes" : "no");
            }
            continue;
        }
        if (strncmp(line, "button", 6) == 0) {
            printf("BUTTON reset=%s download=%s config=%s\n",
                   b2_button_is_pressed(B2_BUTTON_RESET) ? "pressed" : "released",
                   b2_button_is_pressed(B2_BUTTON_DOWNLOAD) ? "pressed" : "released",
                   b2_button_is_pressed(B2_BUTTON_CONFIG) ? "pressed" : "released");
            continue;
        }
        if (strncmp(line, "help", 4) == 0) {
            print_help();
            continue;
        }
        ESP_LOGW(TAG, "unrecognized command: %s", line);
    }
}

esp_err_t b2_console_start(void)
{
    return xTaskCreate(console_task, "b2_console", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
