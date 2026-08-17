# Changelog

All notable changes to this firmware project are documented here. The project follows a practical semantic-versioning convention for firmware releases; hardware acceptance and irreversible security provisioning are called out separately because they cannot be proven by source code or CI alone.

## [Unreleased]

### Release-candidate hardening

- Added reboot-safe event-log flushing before controlled HTTP reboot requests.
- Added authenticated JSON rule management over HTTPS: `GET /api/v1/rules`, `PUT /api/v1/rules/{index}`, and `DELETE /api/v1/rules/{index}`.
- Added mutex-protected live rule reload with validation and edge-latch reset so active rules cannot race HTTP updates.
- Added encrypted-NVS TLS certificate/private-key storage with bounded PEM validation, one-time legacy SD import, and best-effort legacy-file removal. There is no plaintext fallback for HTTPS private keys.
- Added an explicit Secure Boot v2 and release-flash-encryption production profile with documented key custody and irreversible eFuse gates.
- Added opt-in native NimBLE BLE commissioning with encrypted-link enforcement, physical-presence gating, bounded JSON provisioning, and encrypted-NVS persistence. BLE remains disabled by default.
- Added portable behavior helpers shared by firmware and host tests for rule validation, event-ring rollover, SMS token extraction, and settings-version compatibility.
- Added strict host Unity-style behavior tests, cppcheck, and high-signal clang-tidy CI gates. The firmware build depends on the software quality gates.
- Expanded the serial HIL harness with staged read-only checks, explicit persisted-setting checks, JSON reporting, and physical acceptance handoff gates.
- Expanded installation, hardware-integration, capability, and production-security documentation.

### Existing platform capabilities included in this release line

- ESP32-S3 native ESP-IDF firmware for relay outputs, isolated inputs, ADS1115 analog channels, DS18B20/1-Wire, DS3231 RTC, SSD1306 display, RS485 Modbus RTU, SD/SDSPI storage, buttons, SIM7600 AT/SMS/voice/GNSS/APN/PDP, Wi-Fi, MQTT/TLS, Home Assistant discovery, HTTPS diagnostics, OTA rollback, watchdog, brownout handling, NVS encryption, and event telemetry.
- Optional ESP-IDF PPPoS cellular IP mode, W5500 SPI Ethernet, and PCA9548A I2C channel selection, all disabled by default and requiring carrier validation.
- Versioned settings schema with migration support and configurable persisted SNTP server.

## [1.0.0] — Release gate

`v1.0.0` is permitted only after the software gates pass on the tagged commit and the target carrier has a signed hardware acceptance record. The required software gates are:

1. Host project, documentation, and workflow validators.
2. Behavior-level host tests.
3. cppcheck and clang-tidy static analysis.
4. Default ESP-IDF build with optional radio/network features disabled.
5. Opt-in compile matrix for PPP, W5500 Ethernet, PCA9548A expansion, and BLE commissioning.
6. Clean diff, reproducible configuration defaults, and release artifact hash.

The required hardware/manufacturing evidence is separate: relay load/contact-life testing, EMC/ESD/surge and brownout testing, SIM7600 antenna/current/GNSS validation, carrier pin and bus validation, Secure Boot v2 and release flash-encryption provisioning, TLS credential migration verification, and BLE encrypted-link/physical-presence acceptance. A green CI run does not substitute for these tests.

## [0.3.0] — Network parity and field-hardening baseline

- Added configurable SNTP server provisioning and persisted settings schema v4 migration.
- Added deferred/batched NVS event-log persistence with explicit flush support and SD mirroring.
- Added optional native W5500 Ethernet and PCA9548A I2C expansion foundations.
- Added Ethernet/I2C board-profile gates and disabled-safe defaults.
- Added hardening documentation for SD TLS key exposure, EMC/ESD/surge, antenna/current validation, and evidence boundaries.

## [0.2.0] — Cellular, diagnostics, and local intelligence

- Added official Espressif `esp_modem` PPPoS integration as an opt-in mutually exclusive SIM7600 UART mode.
- Added PAP/CHAP APN authentication and exponential-backoff cellular retry behavior.
- Added local rules, analog calibration, SNTP/RTC synchronization, NVS event history, MQTT policy and discovery, and HTTPS diagnostics.
- Added bearer-authenticated relay writes, event export, self-test, controlled reboot, HTTPS OTA rollback, watchdog, and relay fail-safe/interlock handling.

## [0.1.0] — ESP32-S3 controller foundation

- Established the native ESP-IDF project, ESP32-S3 target, partition table, board profile, CI build, host validation, and tag-triggered release workflow.
- Implemented core relay, input, ADC, RTC, OLED, SD, RS485, 1-Wire, physical-button, modem, Wi-Fi, MQTT, storage, and console services.
- Documented the independent implementation boundary and kept Tuya, KinCony Cloud, Loxone, and HomeKit integrations explicitly out of scope.

[Unreleased]: https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller/compare/v0.3.0...HEAD
[1.0.0]: https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller/releases/tag/v1.0.0
[0.3.0]: https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller/releases/tag/v0.3.0
[0.2.0]: https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller/releases/tag/v0.2.0
[0.1.0]: https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller/releases/tag/v0.1.0
