# KinCony B2 Capability Matrix and Implementation Roadmap

## Purpose and evidence boundary

This document compares the publicly described KinCony B2 feature set with the independent ESP32-S3 ESP-IDF firmware in this repository. The commercial B2 announcement identifies an ESP32-S3-WROOM-1U N16R8 module, two relay outputs, four 1-Wire channels, four ADS1115 analog channels, two isolated dry-contact inputs, an SPI SD card, DS3231 RTC, SSD1306 display, RS485, I2C extension, Ethernet, Wi-Fi, Bluetooth, USB-C, a Tuya interface, and optional SIM7600E 4G [1]. The same announcement also describes KCS v3, MQTT Home Assistant discovery, KinCony cloud, Loxone, and Apple HomeKit/Siri integrations [1].

The implementation below is an original project and does not claim to reproduce the commercial schematic, proprietary KCS firmware, Tuya protocol, KinCony cloud service, or closed integrations. Any pin assignment, signal polarity, bus address, electrical scaling, or modem power sequence must be confirmed against the target PCB before field deployment.

## Status legend

| Status | Meaning |
|---|---|
| **Implemented** | Present in the current ESP-IDF application and covered by local validation or build checks. |
| **Implement next** | Open, documented, public capability suitable for an independent ESP-IDF implementation, subject to carrier-board pin verification. |
| **Hardware-gated** | The software can provide an interface, but safe activation requires verified PCB routing, transceiver, level shifting, or electrical scaling. |
| **Protocol-gated** | The capability depends on a public protocol specification or a user-selected server/configuration. |
| **Proprietary/out of scope** | The public product claim depends on closed firmware, cloud credentials, Tuya provisioning, or undocumented vendor protocol; it cannot be honestly implemented as a drop-in replacement from public product pages alone. |

## Capability matrix

| B2 capability | Public evidence | Current repository | Roadmap decision |
|---|---|---|---|
| ESP32-S3-WROOM-1U N16R8 | Official B2 announcement [1] | **Implemented** as the ESP32-S3 build target and native ESP-IDF project. | Keep target-specific build and document flash/PSRAM assumptions. |
| 12–24 V DC input | Official B2 announcement [1] | **Hardware-gated**; firmware does not control the power stage. | Retain electrical validation notes and do not infer protection ratings. |
| Two relay outputs | Official B2 announcement [1] | **Implemented** with safe startup, channel validation, active-high/active-low support, configurable fail-safe/interlock policy, persisted restore policy, local/SMS/MQTT/HTTPS control, and event logging hooks. | Validate hazardous-load interlock behavior on the installed carrier. |
| Two isolated dry-contact inputs | Official B2 announcement [1] | **Implemented** with debouncing, callbacks, rule triggers, MQTT state propagation, and event-log integration. | Add hardware-in-the-loop alarm validation. |
| Four ADS1115 16-bit analog channels | Official B2 announcement [1] | **Implemented** with raw, 0–5 V, 4–20 mA helpers, and persisted per-channel gain/offset calibration. | Validate reference voltage, shunt values, and calibration procedure on the carrier. |
| Four 1-Wire GPIO channels | Official B2 announcement [1] | **Implemented** with native ESP-IDF bit-banging, DS18B20 temperature conversion, ROM reads, presence checks, and CRC validation. | Keep one sensor per configured channel unless a verified multi-drop ROM-selection design is added; confirm external pull-ups and GPIO routing. |
| DS3231 RTC and battery socket | Official B2 announcement [1] | **Implemented** with register reads/writes over I2C, event timestamps, and persisted timezone-aware SNTP boot synchronization. | Validate timezone/DST policy and GNSS-assisted time source on the target installation. |
| SSD1306 display | Official B2 announcement [1] | **Implemented** with four-line status rendering. | Extend status pages for IP, modem state, inputs, analog alarms, storage, and time. |
| SPI SD card | Official B2 announcement [1] | **Implemented** with native SDSPI/FAT mounting, card metadata, bounded text file access, path traversal rejection, and unmount. | Confirm SPI pins, chip-select, voltage translation, card power, and filesystem behavior on the target carrier before enabling production storage. |
| I2C bus extender | Official B2 announcement [1] | **Hardware-gated** with an optional native PCA9548A channel-select helper, persisted board-profile address, and explicit select/deselect API. The feature is disabled by default because the B2-specific extender part and address are not publicly verified. | Confirm the installed extender part, address straps, pull-ups, channel isolation, and sensor topology before enabling. |
| RS485 port | Official B2 announcement [1] | **Implemented** with native UART initialization and a bounded Modbus RTU master supporting function 0x03 and 0x06, CRC checks, exception rejection, and timeouts. | Confirm transceiver direction control, baud/parity, termination, biasing, and device register maps on the target installation. |
| Ethernet RJ45, 100 Mbps IPv4/IPv6 | Official B2 announcement [1] | **Hardware-gated** with an optional native ESP-IDF W5500 SPI Ethernet adapter, ESP-NETIF glue, and explicit board-profile SPI/interrupt/reset pins. The feature is disabled by default because the carrier PHY/transceiver and pin mapping are not verified. | Confirm W5500 presence, magnetics, reset/interrupt wiring, SPI timing, link negotiation, IPv4/IPv6 behavior, and failover policy before enabling. |
| Wi-Fi | ESP32-S3 platform and B2 announcement [1] | **Implemented** with persisted station credentials, reconnect behavior, IP/RSSI status, and local console provisioning. | Add a stronger provisioning flow, credential protection, and production network policy. |
| Bluetooth | B2 announcement [1] | **Not implemented**. | Provide optional BLE commissioning/diagnostics only after a defined GATT model is selected; avoid claiming a generic Bluetooth feature is equivalent to a product integration. |
| USB-C | B2 announcement [1] | **Partially implemented** through the ESP-IDF console/USB connection assumptions. | Document USB serial bring-up and confirm the carrier’s USB-UART/native USB wiring. |
| SIM7600E 4G | B2 announcement [1] and module references [3] | **Implemented** for UART AT transport, modem startup, status, SMS, dialing, hang-up, APN/PDP activation commands, and GNSS query/control APIs. | Add data-session state, retry/recovery, and authenticated command policy; confirm the installed modem variant and antenna routing. |
| SMS relay control | B2/product pages [1][2] | **Implemented** with relay ON/OFF/TOGGLE parsing, sender allow-list policy, replay/rate limiting, optional shared-secret token validation, structured event logging, and state publication. Production defaults require explicit authorization policy. | Validate carrier-specific SMS delivery and operator commissioning procedure. |
| Voice-call control | B2/product pages [1][2] | **Implemented** for dial/hang-up API. | Add incoming-call state machine and caller allow-list before enabling unattended control. |
| Cellular data / GPRS or LTE IP | Product pages and SIM7600 references [2][3] | **Implemented as an opt-in native ESP-IDF PPPoS path** through the official Espressif `esp_modem` component, SIM7600 DCE, PPP netif events, APN PAP/CHAP command provisioning, and cellular transport telemetry. The default remains the legacy AT/SMS/GNSS mode because the modem UART is mutually exclusive. | Enable `CONFIG_B2_CELLULAR_PPP_ENABLED` only after carrier/UART validation; add production retry policy, Wi-Fi/cellular netif failover, and field acceptance testing. |
| GPS/GNSS | SIM7600 module family references [3] | **Implemented as a hardware-gated modem-command service** with enable/disable and bounded `+CGNSSINFO` parsing, including fix, coordinates, UTC, altitude, speed, and satellite count. | Confirm SIM7600 variant, GNSS firmware command set, antenna, sky view, and coordinate semantics on the target carrier. |
| MQTT | Product/KCS references [2][4] | **Implemented as a policy-controlled open ESP-IDF MQTT client** with `mqtts://` certificate verification, explicit plaintext override, credential persistence, relay commands, versioned JSON telemetry for relays/inputs/temperatures/modem/Wi-Fi/events, reconnect behavior, and retained Home Assistant discovery. | Add broker-failover policy and field validation of the selected CA/trust-store provisioning flow. |
| HTTP/REST | Product/KCS references [2][4] | **Implemented** with read-only commissioning endpoints, optional native ESP-IDF HTTPS using SD-provisioned certificate/key files, bearer-authenticated relay POST endpoints on HTTPS, authenticated event/self-test/reboot diagnostics, and explicit plaintext fallback that does not expose writes. | Add authenticated rule CRUD and a production certificate provisioning workflow. |
| TCP client/server | Product/KCS references [2][4] | **Not implemented**. | Add an explicit configurable TCP protocol service only after defining framing, authentication, and timeout behavior. |
| RS485 Modbus | Product/KCS references [2][4] | **Implemented** as a bounded Modbus RTU master with CRC16, timeout, exception, and function 0x03/0x06 handling. | Add configurable device/register maps, optional slave mode, and verified field-bus profiles. |
| Local IFTTT/rules | Product/KCS references [2][4] | **Implemented beyond the public B2 baseline** with persisted rules, input-duration/edge and analog-threshold conditions, signal-quality conditions, relay/SMS/MQTT actions, dedicated evaluation hooks, event logging, and console provisioning for input/ADC relay rules. | Add schedule/RTC and modem-registration condition types plus authenticated HTTP rule CRUD. |
| Home Assistant auto-discovery | KCS references [1][4] | **Implemented as an open MQTT feature** for relay switches, dry-contact binary sensors, analog channels, DS18B20 temperatures, modem CSQ, and Wi-Fi RSSI; normalized telemetry also includes connectivity and event fields. | Add retained discovery entities for storage, RTC synchronization, and modem registration state. |
| Tuya mobile app/module | B2 announcement [1] | **Not implemented**. | Keep as **proprietary/out of scope** unless the user supplies an authorized public protocol/specification and hardware module details. |
| KinCony cloud | B2 announcement [1] | **Not implemented**. | Keep as **proprietary/out of scope**; use open MQTT/HTTP integrations instead. |
| Loxone integration | B2 announcement [1] | **Not implemented**. | Implement only from a public, user-supplied Loxone protocol/configuration requirement. |
| Apple HomeKit/Siri | B2 announcement [1] | **Not implemented**. | Keep separate from core firmware until an approved HomeKit architecture and certification/security model are defined. |
| ESPHome | B2 announcement [1] | **Not applicable to this firmware**. | Continue documenting ESPHome as an alternative firmware ecosystem, not as a runtime dependency of this ESP-IDF project. |
| Tasmota/Matter | Related product page [2] | **Not applicable to this firmware**. | Continue documenting these as alternative firmware options rather than mixing frameworks into the native ESP-IDF build. |
| OTA firmware updates | Project design | **Implemented for verified HTTPS transport and rollback confirmation** through native `esp_https_ota`, CA validation, dual OTA slots, and boot-time `esp_ota_mark_app_valid_cancel_rollback()` handling. | Enable and document Secure Boot v2/image-signing keys on production hardware; this cannot be safely automated from source alone. |
| Three physical buttons | B2 announcement [1] | **Implemented** as configurable, debounced event monitoring for reset, download, and configuration buttons. ROM bootloader/reset behavior remains hardware-controlled. | Confirm active levels, pull resistors, and boot-pin electrical behavior on the target carrier before assigning actions. |

## Implementation order

The local capability tranche now includes four-channel 1-Wire/DS18B20, SD-card FAT mounting, physical-button monitoring, versioned NVS settings, Modbus RTU master operations, calibrated analog channels, SMS authorization/rate/replay policy, Wi-Fi station mode, an open MQTT client, retained Home Assistant discovery, structured event logging, relay safety policy, a local rule engine, and cross-service MQTT event/state publication. These services use native ESP-IDF APIs and keep carrier-specific wiring configurable.

The modem capability tranche now includes direct APN/PDP commands, persisted PAP/CHAP fields, hardware-gated GNSS control/query APIs, and an optional official Espressif `esp_modem` PPPoS service. `CONFIG_B2_CELLULAR_PPP_ENABLED=0` preserves the legacy AT/SMS/GNSS service; enabling it gives the SIM7600 exclusive UART ownership, creates a PPP netif, applies APN authentication, and exposes cellular connectivity state. Carrier retry/failover validation remains a deployment requirement.

The diagnostics tranche now includes a native ESP-IDF HTTP/HTTPS server with bounded JSON responses for health, capabilities, normalized status, event history, self-test, and controlled reboot. Read-only commissioning endpoints remain available without authentication; relay and remote-diagnostics controls require a configured bearer token and are registered only on the HTTPS listener when `server.crt` and `server.key` are present on the SD card. Control requests share a ten-second rate limit, and plain HTTP fallback intentionally exposes no remote-control path.

The reviewed follow-up tranche now includes opt-in W5500 Ethernet and PCA9548A expansion-bus foundations behind explicit board-profile and Kconfig gates, configurable SNTP server provisioning, and deferred event-log NVS persistence. Transport-priority failover, authenticated rule CRUD, and retained discovery for storage/RTC/modem state remain future work. The current services already share versioned MQTT/HTTP state shapes, and the optional PPP service is present, but carrier and Ethernet hardware-in-the-loop testing is still required before field deployment.

The remaining production tranche is carrier-validated PPP/LTE operation, Ethernet/BLE hardware profiles, transport failover, and Secure Boot v2/image-signing provisioning. HTTPS OTA rollback handling, encrypted NVS initialization, watchdog recovery, TLS policy, and credential protection are now represented in the firmware; key burning and carrier-board validation remain deployment responsibilities.

Tuya, KinCony cloud, Loxone, and Apple HomeKit should remain explicit integration boundaries rather than being represented as falsely complete features. They can be added later if the user provides the authorized protocol, service endpoint, provisioning requirements, and hardware module documentation.

## References

[1]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller released"
[2]: https://www.kincony.com/smart-electrical-distribution-panel-tuya-home-assistant-4ch.html "KinCony smart electrical distribution panel product page"
[3]: https://www.kincony.com/esp32-s3-sim7600e-4g-module.html "KinCony ESP32-S3 SIM7600E 4G module"
[4]: https://www.kincony.com/forum/showthread.php?tid=7618 "KinCony KCS v3 RS485 Modbus protocol document"
[5]: https://github.com/K0I05/KINCONY-S3_RTU "KinCony S3 RTU/I2C/1-Wire demo repository"
