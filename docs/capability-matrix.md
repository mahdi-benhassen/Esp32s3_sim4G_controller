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
| Two relay outputs | Official B2 announcement [1] | **Implemented** with safe startup, channel validation, active-high/active-low support, and local/SMS control. | Add persistent state policy, interlocks, event logging, and optional timed actions. |
| Two isolated dry-contact inputs | Official B2 announcement [1] | **Implemented** with debouncing and callbacks. | Add configurable edge actions, input event records, and alarm notification hooks. |
| Four ADS1115 16-bit analog channels | Official B2 announcement [1] | **Implemented** with raw, 0–5 V, and 4–20 mA helpers. | Add calibration/persistence and explicit per-channel scaling configuration. |
| Four 1-Wire GPIO channels | Official B2 announcement [1] | **Implemented** with native ESP-IDF bit-banging, DS18B20 temperature conversion, ROM reads, presence checks, and CRC validation. | Keep one sensor per configured channel unless a verified multi-drop ROM-selection design is added; confirm external pull-ups and GPIO routing. |
| DS3231 RTC and battery socket | Official B2 announcement [1] | **Implemented** with register reads/writes over I2C. | Add boot-time system-clock synchronization and persisted timezone/validity policy. |
| SSD1306 display | Official B2 announcement [1] | **Implemented** with four-line status rendering. | Extend status pages for IP, modem state, inputs, analog alarms, storage, and time. |
| SPI SD card | Official B2 announcement [1] | **Implemented** with native SDSPI/FAT mounting, card metadata, bounded text file access, path traversal rejection, and unmount. | Confirm SPI pins, chip-select, voltage translation, card power, and filesystem behavior on the target carrier before enabling production storage. |
| I2C bus extender | Official B2 announcement [1] | **Not implemented**; no B2-specific extender part/address is publicly verified. | Add an abstract bus-expander interface only after the target component and address are confirmed. |
| RS485 port | Official B2 announcement [1] | **Implemented** with native UART initialization and a bounded Modbus RTU master supporting function 0x03 and 0x06, CRC checks, exception rejection, and timeouts. | Confirm transceiver direction control, baud/parity, termination, biasing, and device register maps on the target installation. |
| Ethernet RJ45, 100 Mbps IPv4/IPv6 | Official B2 announcement [1] | **Not implemented**; the PHY, MAC mode, and pins are not verified for the custom carrier. | Add ESP-NETIF/Ethernet support after PHY and pin mapping are confirmed; include IPv4 first and IPv6 validation. |
| Wi-Fi | ESP32-S3 platform and B2 announcement [1] | **Implemented** with persisted station credentials, reconnect behavior, IP/RSSI status, and local console provisioning. | Add a stronger provisioning flow, credential protection, and production network policy. |
| Bluetooth | B2 announcement [1] | **Not implemented**. | Provide optional BLE commissioning/diagnostics only after a defined GATT model is selected; avoid claiming a generic Bluetooth feature is equivalent to a product integration. |
| USB-C | B2 announcement [1] | **Partially implemented** through the ESP-IDF console/USB connection assumptions. | Document USB serial bring-up and confirm the carrier’s USB-UART/native USB wiring. |
| SIM7600E 4G | B2 announcement [1] and module references [3] | **Implemented** for UART AT transport, modem startup, status, SMS, dialing, hang-up, APN/PDP activation commands, and GNSS query/control APIs. | Add data-session state, retry/recovery, and authenticated command policy; confirm the installed modem variant and antenna routing. |
| SMS relay control | B2/product pages [1][2] | **Implemented** with relay ON/OFF/TOGGLE parsing, persistent relay-state support, and configurable sender allow-list mode. The default remains allow-all until commissioning changes it. | Add replay/rate limiting, structured acknowledgements, and a secure commissioning command path. |
| Voice-call control | B2/product pages [1][2] | **Implemented** for dial/hang-up API. | Add incoming-call state machine and caller allow-list before enabling unattended control. |
| Cellular data / GPRS or LTE IP | Product pages and SIM7600 references [2][3] | **Partially implemented** with persisted APN storage and direct SIM7600 PDP attach/activation commands; no ESP-IDF PPP/IP netif is created yet. | Implement PPP/IP transport, session state, retry/backoff, and carrier-specific validation. |
| GPS/GNSS | SIM7600 module family references [3] | **Implemented as a hardware-gated modem-command service** with enable/disable and bounded `+CGNSSINFO` parsing, including fix, coordinates, UTC, altitude, speed, and satellite count. | Confirm SIM7600 variant, GNSS firmware command set, antenna, sky view, and coordinate semantics on the target carrier. |
| MQTT | Product/KCS references [2][4] | **Partially implemented** as an open ESP-IDF MQTT client over Wi-Fi with relay command topics, JSON state publication, reconnect behavior, NVS configuration, retained Home Assistant discovery entities, and input/SMS state propagation. | Add certificate/trust-store provisioning, broker authorization policy, broader telemetry, and production credential protection. |
| HTTP/REST | Product/KCS references [2][4] | **Implemented as read-only diagnostics** with `GET /health`, `/api/v1/capabilities`, and `/api/v1/status`; no write operations, authentication, or TLS. | Add authenticated relay control only after defining credentials, TLS, authorization, rate limits, and replay protection. |
| TCP client/server | Product/KCS references [2][4] | **Not implemented**. | Add an explicit configurable TCP protocol service only after defining framing, authentication, and timeout behavior. |
| RS485 Modbus | Product/KCS references [2][4] | **Implemented** as a bounded Modbus RTU master with CRC16, timeout, exception, and function 0x03/0x06 handling. | Add configurable device/register maps, optional slave mode, and verified field-bus profiles. |
| Local IFTTT/rules | Product/KCS references [2][4] | **Not implemented**. | Implement a local rule engine driven by debounced inputs, analog thresholds, schedules, modem events, and relay actions. |
| Home Assistant auto-discovery | KCS references [1][4] | **Implemented as an open MQTT feature** for two relay switches and two dry-contact binary sensors, published retained under the standard `homeassistant/.../config` topics. | Add discovery for analog, temperature, modem, and connectivity entities after the normalized telemetry model and TLS policy are complete. |
| Tuya mobile app/module | B2 announcement [1] | **Not implemented**. | Keep as **proprietary/out of scope** unless the user supplies an authorized public protocol/specification and hardware module details. |
| KinCony cloud | B2 announcement [1] | **Not implemented**. | Keep as **proprietary/out of scope**; use open MQTT/HTTP integrations instead. |
| Loxone integration | B2 announcement [1] | **Not implemented**. | Implement only from a public, user-supplied Loxone protocol/configuration requirement. |
| Apple HomeKit/Siri | B2 announcement [1] | **Not implemented**. | Keep separate from core firmware until an approved HomeKit architecture and certification/security model are defined. |
| ESPHome | B2 announcement [1] | **Not applicable to this firmware**. | Continue documenting ESPHome as an alternative firmware ecosystem, not as a runtime dependency of this ESP-IDF project. |
| Tasmota/Matter | Related product page [2] | **Not applicable to this firmware**. | Continue documenting these as alternative firmware options rather than mixing frameworks into the native ESP-IDF build. |
| OTA firmware updates | Project design | **Partially implemented** through OTA-capable partitions and tagged release artifacts. | Add authenticated OTA transport, version policy, rollback verification, and signed-image strategy. |
| Three physical buttons | B2 announcement [1] | **Implemented** as configurable, debounced event monitoring for reset, download, and configuration buttons. ROM bootloader/reset behavior remains hardware-controlled. | Confirm active levels, pull resistors, and boot-pin electrical behavior on the target carrier before assigning actions. |

## Implementation order

The first implementation tranche is complete for the local, testable capabilities: four-channel 1-Wire/DS18B20, SD-card FAT mounting, physical-button monitoring, versioned NVS settings, Modbus RTU master operations, SMS sender authorization policy, Wi-Fi station mode, an open MQTT client, retained Home Assistant discovery, and cross-service MQTT state publication for input and SMS events. These services use native ESP-IDF APIs and keep carrier-specific wiring configurable.

The modem capability tranche now includes direct APN/PDP commands and hardware-gated GNSS control/query APIs. The project still does not claim a complete LTE IP data path: direct PDP activation is not the same as an ESP-IDF PPP netif, routable sockets, or automatic data-session recovery.

The local diagnostics tranche now includes a native ESP-IDF HTTP server with bounded JSON responses for health, capabilities, and normalized status. It is deliberately read-only and unauthenticated so it can support commissioning without silently becoming an unsafe remote-control surface.

The next tranche should add Ethernet behind a verified PHY profile, a local rule engine, MQTT TLS/trust-store provisioning, broader Home Assistant telemetry, and authenticated HTTP services. The read-only HTTP foundation is now present, but it must not be treated as an operator API. These services should share one normalized device-state model so relay, input, analog, modem, RTC, storage, and connectivity state are not implemented independently for each protocol.

A later tranche should add PPP/LTE data, secure OTA, and production credential handling. These features require careful watchdog and retry policy, TLS certificate management, and field testing with the actual SIM7600 variant and carrier board.

Tuya, KinCony cloud, Loxone, and Apple HomeKit should remain explicit integration boundaries rather than being represented as falsely complete features. They can be added later if the user provides the authorized protocol, service endpoint, provisioning requirements, and hardware module documentation.

## References

[1]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller released"
[2]: https://www.kincony.com/smart-electrical-distribution-panel-tuya-home-assistant-4ch.html "KinCony smart electrical distribution panel product page"
[3]: https://www.kincony.com/esp32-s3-sim7600e-4g-module.html "KinCony ESP32-S3 SIM7600E 4G module"
[4]: https://www.kincony.com/forum/showthread.php?tid=7618 "KinCony KCS v3 RS485 Modbus protocol document"
[5]: https://github.com/K0I05/KINCONY-S3_RTU "KinCony S3 RTU/I2C/1-Wire demo repository"
