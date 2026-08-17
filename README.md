# B2-compatible ESP32-S3 2-channel 4G smart controller

This repository contains an **original ESP-IDF firmware reference implementation** for an ESP32-S3 DIN-rail smart controller inspired by the public KinCony B2 feature set. It is not KinCony firmware, does not copy proprietary source code, and is intended for an original carrier PCB or a carefully adapted prototype.

> **Important hardware note:** the GPIO assignments in `main/b2_config.c` are an example board profile. Product photographs and public product descriptions do not uniquely determine the actual ESP32-S3 GPIO routing. Before connecting a modem or field wiring, replace the profile with the schematic for your own PCB and verify every GPIO, voltage level, boot strap, relay driver, modem power key, and RS485 transceiver connection.

## Feature coverage

The official B2 announcement describes an ESP32-S3-WROOM-1U controller with two relays, two isolated dry-contact inputs, four ADS1115 analog channels, four 1-Wire channels, a DS3231 RTC, SSD1306 I2C display, SD/SPI, RS485, Ethernet, Wi-Fi, USB-C, and SIM7600E 4G support [1]. This project implements the core local-control path in native ESP-IDF C:

| Subsystem | Implementation in this repository | Adaptation point |
| --- | --- | --- |
| ESP32-S3 application | `app_main.c`, FreeRTOS tasks, NVS initialization | `main/` |
| Two relay outputs | GPIO driver with active-high/active-low support and mutex-protected state | `main/b2_relay.c` |
| Two dry inputs | GPIO sampling with 50 ms debounce and event callback | `main/b2_inputs.c` |
| Four analog channels | ADS1115 single-shot I2C reads; channels 1–2 exposed as voltage and 3–4 as 4–20 mA helpers | `main/b2_adc.c` |
| DS3231 RTC | BCD register read/write over I2C | `main/b2_rtc.c` |
| SSD1306 display | Minimal native I2C initialization and four-line text renderer | `main/b2_oled.c` |
| SIM7600 | Native ESP-IDF UART AT transport, modem power/reset sequence, SMS command parsing, SMS sending, dialing, hang-up, and registration queries | `main/b2_modem.c` |
| RS485 | Native ESP-IDF UART half-duplex mode | `main/b2_board.c` |
| Local bring-up console | stdin command task compatible with `idf.py monitor` | `main/b2_console.c` |
| SD card | Pin map reserved in `b2_config.c`; FAT/SPI mount is deliberately left for the final PCB schematic | `main/b2_config.c` |
| Wi-Fi/Ethernet/MQTT/HTTP | Not enabled in this first firmware slice; the application boundaries are prepared for adding ESP-IDF network services without changing relay or modem APIs | future component |

The public KinCony GitHub material inspected for this project is primarily ESPHome configuration and Arduino API material rather than a B2-specific ESP-IDF source tree [2] [3]. The implementation therefore keeps the behavior conceptually compatible while remaining an independent codebase.

## Example board profile

The default profile is intentionally conservative and must be edited for the target PCB. The exact table below is only a build-time example, not a claim about the commercial KinCony B2 pinout.

| Function | Example GPIO or interface | Electrical caution |
| --- | --- | --- |
| Relay 1 / Relay 2 | GPIO4 / GPIO5 | Drive the relay transistor or driver input, never a coil directly. |
| Dry input 1 / Dry input 2 | GPIO6 / GPIO7 | Profile assumes active-low inputs with external isolation and pull-ups. |
| 1-Wire 1–4 | GPIO8–GPIO11 | One-Wire implementation is reserved for the next component; validate pull-up voltage. |
| Modem power/reset/ring | GPIO12 / GPIO13 / GPIO14 | SIM7600 power-key polarity and pulse timing must match the modem carrier. |
| SIM7600 UART1 TX/RX | GPIO17 / GPIO18 | Use the carrier's required logic level and common ground. |
| RS485 UART2 TX/RX/RTS | GPIO15 / GPIO16 / GPIO21 | Add the transceiver's fail-safe bias and termination as required. |
| Shared I2C SDA/SCL | GPIO38 / GPIO39 | Intended for ADS1115, DS3231, and SSD1306 at 3.3 V. |
| SD/SPI reserved pins | GPIO34–GPIO37 | The storage mount is not enabled until the final SD wiring is confirmed. |

## SMS control

After the SIM7600 has registered and the modem has been initialized, SMS messages can control relays using the following commands. The parser is intentionally small and should be extended with sender allow-listing and authentication before deployment on an unattended installation.

| SMS body | Result |
| --- | --- |
| `RELAY1 ON` | Energizes relay channel 1. |
| `RELAY1 OFF` | De-energizes relay channel 1. |
| `RELAY1 TOGGLE` | Toggles relay channel 1. |
| `RELAY2 ON`, `RELAY2 OFF`, `RELAY2 TOGGLE` | Performs the equivalent operation on channel 2. |

The current reference driver supports text-mode SMS, voice dialing, hang-up, and basic `CSQ`, `CREG`, and `CGATT` status queries. It does not yet implement a packet-data PPP or IP session. That separation is deliberate: the SIM7600 AT transport can be validated on the bench before adding APN credentials and a network-facing protocol.

## Build with native ESP-IDF

Install ESP-IDF 5.x using Espressif's official installation instructions, export the environment, and set the target to ESP32-S3. The repository intentionally uses the native `idf.py` build flow instead of Arduino, PlatformIO, or MicroPython.

```bash
git clone https://github.com/espressif/esp-idf.git --branch v5.3.2 --depth 1
cd esp-idf
./install.sh esp32s3
. ./export.sh

cd /path/to/kincony-b2-esp32s3-idf
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The project defaults target an 8 MB ESP32-S3 flash device and use two OTA application slots. If the module or carrier has a different flash size, adjust `sdkconfig.defaults` and regenerate the project configuration before flashing.

## CI/CD and releases

GitHub Actions are defined under `.github/workflows/`. Every pull request, push to `main`, and manual dispatch runs host validation and a native ESP-IDF ESP32-S3 build. The host job checks the repository structure, ESP-IDF CMake metadata, target selection, partition table, native-library policy, Python syntax, and repository whitespace without requiring hardware. The firmware job compiles the application using ESP-IDF `v5.3.2`, uploads the bootloader, partition table, OTA data, application binary, partition CSV, and SHA-256 checksums as a short-lived CI artifact.

A semantic version tag matching `vMAJOR.MINOR.PATCH`, such as `v1.0.0`, starts the release workflow. It validates the exact tagged source, rebuilds the firmware from that tag, packages the flashable binaries and documentation into `kincony-b2-esp32s3-vMAJOR.MINOR.PATCH.tar.gz`, generates checksums, and creates a GitHub Release with generated notes. To publish a release locally after committing the desired version, use:

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

The same release workflow can be started manually from the GitHub Actions interface for an existing semantic-version tag. Run the fast local checks before opening a pull request:

```bash
python3 -m py_compile test_host/test_project.py
python3 test_host/test_project.py
git diff --check
```

## Bring-up sequence

First validate the board with field wiring disconnected and a current-limited 12–24 V DC supply whose downstream regulator produces the correct 3.3 V rail. Confirm that the relays start de-energized. Then validate I2C devices one at a time, followed by RS485 loopback, modem UART communication, SIM7600 registration, and finally dry-contact inputs. Do not connect mains wiring during firmware bring-up; relay contact ratings, creepage, clearance, fusing, enclosure, and installation requirements must be reviewed by a qualified electrical professional.

The local monitor accepts commands such as `relay 1 on`, `relay 2 toggle`, `input`, `adc 1`, and `modem`. Use these commands to verify each subsystem before adding a network protocol.

## Repository structure

```text
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── b2_*.c
│   └── include/b2_*.h
├── docs/
└── test_host/
```

## Safety and production hardening

The reference firmware intentionally favors explicit, inspectable C and ESP-IDF driver calls. For production, add a signed OTA strategy, watchdog policy, persistent configuration validation, modem sender authorization, TLS credentials, APN storage in encrypted NVS, relay fail-safe rules, brownout testing, surge protection, and a hardware interlock for hazardous loads. The modem and analog front end need their own power integrity, ESD, EMC, and antenna validation.

## References

[1]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller released"

[2]: https://github.com/hzkincony/kc868-a4 "KinCony kc868-a4 ESPHome project template"

[3]: https://github.com/hzkincony/kc868-arduino-library "KinCony kc868 Arduino library"

[4]: https://www.kincony.com/smart-electrical-distribution-panel-tuya-home-assistant-4ch.html "KinCony smart electrical distribution panel overview"
