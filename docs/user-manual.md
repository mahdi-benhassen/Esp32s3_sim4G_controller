# ESP32-S3 SIM7600 Controller — Installation and User Manual

## 1. Read this before installation

This manual is for a technician or developer installing the firmware on an original ESP32-S3 carrier board with a compatible SIM7600 modem and the peripherals described in the project documentation. It is not a wiring certification for the commercial KinCony B2 product, and it cannot replace the schematic, assembly instructions, electrical inspection, or regulatory review for the target hardware [1] [2].

> **Safety boundary:** Perform all firmware bring-up with relay loads disconnected and with no mains wiring attached. Use a current-limited low-voltage supply appropriate for the assembled carrier board. A qualified electrical professional must review relay ratings, fusing, creepage, clearance, enclosure, grounding, surge protection, antenna installation, and local installation requirements before hazardous loads are connected.

Do not assume that the example GPIO profile is correct for your board. The firmware builds with a reference mapping, but the actual carrier schematic must determine every GPIO, polarity, voltage level, boot strap, connector, and modem control signal [3].

## 2. What the system provides

After installation, the firmware provides two local relay outputs, two debounced dry-contact inputs, four ADS1115 analog channels, four configurable 1-Wire/DS18B20 channels, SPI SD-card storage, three debounced physical-button inputs, RS485 UART with Modbus RTU master helpers, a DS3231 RTC interface, an SSD1306 OLED status view, a SIM7600 AT-command service with SMS, voice, APN/PDP, and hardware-gated GNSS controls, persisted NVS settings, Wi-Fi station mode, an optional MQTT client with retained Home Assistant discovery, a read-only HTTP diagnostics API, and a serial bring-up console. The reference application does not provide a PPP/IP cellular data session, authenticated HTTP write operations, proprietary Tuya/KCS integration, encrypted NVS credentials, or a complete application-level access-control policy [4].

| Function | User-visible behavior |
|---|---|
| Relay 1 and Relay 2 | Safe de-energized startup; local console ON/OFF/TOGGLE commands; SMS ON/OFF/TOGGLE commands. |
| Dry inputs | Debounced state changes are logged; current firmware does not automatically switch relays from input state. |
| Analog channels | Console reads voltage on channels 1–2 and current helper values on channels 3–4. |
| SIM7600 | AT transport, registration and signal status, incoming SMS parsing, SMS sending API, dialing, hang-up, APN/PDP commands, and GNSS enable/query APIs. |
| OLED | Periodic display of relay states, modem registration/signal, and input states. |
| RS485 | Native ESP-IDF UART half-duplex initialization; application protocol is not included. |
| RTC | DS3231 read/write API over shared I2C; application time policy is not included. |
| 1-Wire / DS18B20 | Four configurable bit-banged 1-Wire channels with ROM discovery, temperature conversion, and CRC validation. |
| SD card | Optional SDSPI FAT mounting and safe application-path file access; verify SPI routing before insertion. |
| Physical buttons | Debounced reset/download/configuration events; bootloader behavior remains controlled by the ESP32-S3 ROM and board design. |
| Modbus RTU | Bounded RS485 master reads and writes with CRC16 validation; slave register maps remain application-specific. |
| Wi-Fi | Persisted station credentials, reconnect behavior, IP/RSSI status, and local provisioning commands. |
| MQTT | Optional broker client with relay command topics, retained JSON state, and retained Home Assistant discovery for relays and dry-contact sensors; configure TLS and access policy for production. |
| HTTP diagnostics | Read-only `GET /health`, `/api/v1/capabilities`, and `/api/v1/status` endpoints on port 80 when the service is enabled; keep it LAN-only until authentication and TLS are added. |

## 3. Required equipment and information

Prepare the target ESP32-S3 carrier board, compatible SIM7600 carrier and antennas, a suitable low-voltage supply, USB data cable, computer, cellular SIM with the required service enabled, and the target board schematic. The computer must have Git, Python 3, and an ESP-IDF 5.x installation that includes the ESP32-S3 toolchain. The repository is designed around ESP-IDF `v5.3.2` and the native `idf.py` workflow [5].

Before applying power, identify the board’s USB-UART interface, boot/download button, reset button, modem power/reset signals, SIM7600 UART pins, relay driver inputs, dry-input terminals, I2C devices, RS485 transceiver, and supply polarity. Confirm that the SIM7600 carrier has a power source capable of its transient demand; do not power a modem carrier from an unverified ESP32-S3 regulator output.

## 4. Hardware preparation

### 4.1 Confirm the board profile

Open `main/b2_config.c` and compare every assignment with the target schematic. The default example is summarized below.

| Signal group | Example pins | Action before flashing |
|---|---:|---|
| Relays | GPIO4, GPIO5 | Confirm driver polarity and safe reset state. |
| Dry inputs | GPIO6, GPIO7 | Confirm isolation, active-low behavior, and input voltage. |
| Modem controls | GPIO12, GPIO13, GPIO14 | Confirm power-key, reset, and ring polarity/timing. |
| Modem UART1 | TX GPIO17, RX GPIO18 | Confirm cross-over and logic-level compatibility. |
| RS485 UART2 | TX GPIO15, RX GPIO16, RTS GPIO21 | Confirm DE/RE direction control and transceiver wiring. |
| I2C | SDA GPIO38, SCL GPIO39 | Confirm ADS1115, DS3231, and SSD1306 addresses and pull-ups. |
| SD/SPI reservation | MOSI GPIO35, MISO GPIO36, SCLK GPIO37, CS GPIO34 | Do not enable storage until the PCB routing is verified. |
| Boot/configuration | GPIO0, GPIO45, GPIO46 | Keep external circuitry from forcing an unintended boot mode. |

If the mapping changes, rebuild and test the new profile before connecting field wiring. The profile includes `relay_active_high` and `dry_input_active_low` flags; set them to match the actual driver and input circuits.

### 4.2 Perform the low-voltage inspection

With the board unpowered, inspect for solder bridges, reversed connectors, unpopulated protection parts, incorrect antenna connectors, and damaged USB or SIM holders. Confirm that the modem and ESP32-S3 grounds are connected as intended by the schematic, while preserving any required isolation barrier around field I/O.

Apply power with relay loads disconnected. Measure the regulated rails and monitor the board for overheating. Confirm that the relays remain de-energized during reset, bootloader entry, and application startup. If a relay energizes unexpectedly, remove power immediately and correct the carrier hardware or polarity profile before proceeding.

## 5. Install ESP-IDF and build the firmware

The following commands install a shallow ESP-IDF checkout and prepare the ESP32-S3 tools. Follow Espressif’s platform-specific prerequisites if the computer does not already have the required compiler, CMake, Ninja, Python, and USB permissions [5].

```bash
git clone https://github.com/espressif/esp-idf.git --branch v5.3.2 --depth 1
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

Clone the controller repository and select the target:

```bash
git clone https://github.com/mahdi-benhassen/Esp32s3_sim4G_controller.git
cd Esp32s3_sim4G_controller
idf.py set-target esp32s3
```

Review `main/b2_config.c`, `sdkconfig.defaults`, and `partitions.csv`. The default partition table expects an 8 MB flash device. If the module has another flash size, update the configuration only after confirming the module specification and regenerate the project configuration.

Build before connecting field wiring:

```bash
idf.py build
```

A successful build produces the application and flashing binaries under `build/`. The build must complete without compiler errors. Warnings or unexpected configuration changes should be investigated rather than ignored.

## 6. Connect the board and flash the firmware

Connect a USB data cable to the ESP32-S3 programming interface. Identify the serial device before flashing. On Linux it is commonly `/dev/ttyACM0` or `/dev/ttyUSB0`; on Windows use the assigned COM port; on macOS use the relevant `/dev/cu.*` device.

If the board is not detected, hold the board’s download/boot button while pressing reset, then release the download button when the ROM bootloader is active. The exact procedure depends on the carrier board.

Flash and open the monitor with:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Replace the port with the actual device. To erase the flash before a clean first installation, use this only when required by the board procedure or when an incompatible previous configuration is suspected:

```bash
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

Erasing flash removes NVS data and any application configuration stored there. It does not fix an incorrect GPIO mapping or an unsafe carrier design.

## 7. First-boot commissioning

Keep relay loads, field sensors, and external RS485 equipment disconnected during the first boot. Observe the serial monitor at 115200 baud, which is the project default. The firmware should initialize NVS, board buses, relays, inputs, optional peripherals, modem service, console, and status display. A peripheral that is absent or not yet wired may produce a warning while the application continues.

Use the following commissioning sequence:

| Step | Test | Expected result |
|---:|---|---|
| 1 | Observe reset and boot | Board boots without forcing an unintended download mode. |
| 2 | Check relay outputs | Both relays remain de-energized at startup. |
| 3 | Type `help` | Console prints the supported command list. |
| 4 | Test one relay at a time | The expected driver input changes; connect no hazardous load. |
| 5 | Type `input` | Both dry-input states are reported. |
| 6 | Type `adc 1` through `adc 4` | Connected and correctly wired channels return plausible values. |
| 7 | Type `modem` | UART readiness, registration, attachment, and CSQ are reported. |
| 8 | Validate the OLED | Relay, modem, and input status refreshes approximately every five seconds. |
| 9 | Test RS485 locally | Use a controlled loopback or known-good test device. |
| 10 | Connect field wiring | Only after low-voltage tests and electrical review pass. |

## 8. Local console commands

The console is available through the ESP-IDF monitor’s standard input. Commands are case-sensitive and must be followed by Enter.

| Command | Description | Example output or result |
|---|---|---|
| `help` | Prints the supported commands. | `Commands: relay <1\|2> <on\|off\|toggle>, input, adc <1..4>, modem, help` |
| `relay 1 on` | Energizes relay channel 1 according to configured polarity. | No structured response; inspect hardware and logs. |
| `relay 1 off` | De-energizes relay channel 1. | No structured response; relay should return to safe state. |
| `relay 1 toggle` | Inverts relay channel 1 state. | No structured response; inspect hardware and OLED. |
| `relay 2 on/off/toggle` | Performs the equivalent operation for channel 2. | Same as channel 1. |
| `input` | Reads both logical dry-input states. | `INPUT1=ON INPUT2=OFF` |
| `adc 1` or `adc 2` | Reads a voltage helper for ADS1115 channel 1 or 2. | `ADC1=1.234 V` |
| `adc 3` or `adc 4` | Reads a 4–20 mA helper for ADS1115 channel 3 or 4. | `ADC3=12.345 mA` |
| `modem` | Queries modem registration and signal status. | `MODEM registered=yes attached=no CSQ=18` |
| `modem gnss on/off/read` | Enables or disables GNSS, or reads the latest `+CGNSSINFO` fix data. | Reports fix validity, satellite count, UTC, latitude, longitude, altitude, and speed. |
| `modem apn <operator-apn>` | Persists and applies the SIM7600 APN profile. | Reports `saved and applied` or a bounded modem error. |
| `modem pdp` | Activates the configured SIM7600 packet-data context. | Reports PDP activation status; this does not create a PPP/IP netif. |
| `onewire` | Reads DS18B20 sensor status/temperature for channel 1–4. | Reports ROM, presence, CRC, and temperature. |
| `storage` | Reports SD-card mount and metadata. | Reports mounted state, capacity, and product identifier. |
| `button` | Reports configured physical-button states/events. | Reports reset/download/configuration input states. |
| `modbus read <id> <reg> <count>` | Reads holding registers from an RS485 Modbus RTU slave. | Returns register values or a bounded error. |
| `modbus write <id> <reg> <value>` | Writes one holding register. | Returns transaction status. |
| `wifi` | Queries Wi-Fi station state. | Reports enabled state, association, IP, SSID, and RSSI. |
| `wifi set <ssid> <password>` | Stores station credentials in NVS; reboot is required. | Reports saved or validation error. |
| `wifi off` | Disables persisted Wi-Fi station startup. | Reports saved state; reboot is required. |
| `mqtt` | Queries MQTT state and broker/topic configuration. | Reports started, connected, URI, topic, and last message ID. |
| `mqtt set <mqtt[s]://broker> [username] [password]` | Stores MQTT broker settings in NVS; reboot is required. | Reports saved or validation error. |
| `mqtt off` | Disables persisted MQTT startup. | Reports saved state; reboot is required. |
| `http` | Reports the read-only HTTP service and endpoint list. | Reports port 80 and `/health`, `/api/v1/status`, `/api/v1/capabilities`. |

The console is a bring-up interface, not an authenticated operator interface. Do not expose it to an untrusted network or connect the monitor to a shared production console without access control.

## 9. SIM7600 installation and SMS control

Install the SIM card and antennas according to the modem carrier documentation. Confirm that the SIM is provisioned, unlocked if required, and allowed to register on the intended network. The modem must have a suitable supply, ground, UART level compatibility, and correct power-key/reset wiring.

After boot, type `modem` periodically until the modem reports registration. Signal quality varies with the network and antenna environment. Use `modem apn <operator-apn>` to store and apply the carrier APN, then `modem pdp` to request packet-data activation. Use `modem gnss on` and `modem gnss read` only when the installed SIM7600 variant supports the GNSS command set and the GNSS antenna is connected with a suitable sky view. Direct PDP activation is not an ESP-IDF PPP/IP data session and does not by itself provide sockets or Internet access. A modem that does not respond to initialization should be tested separately with a minimal `AT` command path before troubleshooting application SMS behavior.

The application accepts the following SMS bodies after converting lowercase letters to uppercase:

| SMS body | Action |
|---|---|
| `RELAY1 ON` | Energizes relay 1. |
| `RELAY1 OFF` | De-energizes relay 1. |
| `RELAY1 TOGGLE` | Toggles relay 1. |
| `RELAY2 ON` | Energizes relay 2. |
| `RELAY2 OFF` | De-energizes relay 2. |
| `RELAY2 TOGGLE` | Toggles relay 2. |

The reference parser does not authenticate the sender or return a confirmation SMS. Before unattended use, add a sender allow-list, message authentication, rate limiting, command acknowledgement policy, and a fail-safe behavior for modem loss. Never use SMS control as the only protection for hazardous equipment.

### 9.1 Wi-Fi and MQTT commissioning

Wi-Fi and MQTT are independent of the SIM7600 SMS path. Configure Wi-Fi first, then verify `wifi` reports an IP address. Configure MQTT with a reachable `mqtt://` or `mqtts://` URI. The client subscribes to `<base-topic>/relay/1/set` and `<base-topic>/relay/2/set`; payloads are `ON`, `OFF`, or `TOGGLE`. It publishes a JSON state message to `<base-topic>/state`, for example `{"relay1":false,"relay2":true,"input1":0,"input2":1}`. The default base topic is `b2/controller`.

The reference implementation stores broker credentials in the versioned NVS settings blob and does not provision certificates or enforce broker authorization. Use `mqtts://` with a properly configured ESP-IDF TLS trust model and add credential protection before production deployment. When connected, the client publishes retained Home Assistant discovery documents for the two relays and two dry-contact inputs under the standard `homeassistant/.../config` topics. Do not expose the local console, read-only HTTP service, or an unauthenticated broker to an untrusted network.

### 9.2 Read-only HTTP diagnostics

When the HTTP service is running, use the controller’s Wi-Fi address from another device on the same trusted LAN:

```bash
curl http://CONTROLLER_IP/health
curl http://CONTROLLER_IP/api/v1/capabilities
curl http://CONTROLLER_IP/api/v1/status
```

The status endpoint reports relay state, dry-input state, Wi-Fi association and RSSI, MQTT connection state, and SIM7600 registration/attachment/signal information. It does not accept relay write commands. The service has no authentication or TLS in this reference implementation; treat it as a commissioning and trusted-LAN diagnostic feature only.

## 10. Updating firmware

For a source build from the current `main` branch:

```bash
cd Esp32s3_sim4G_controller
git pull --ff-only origin main
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The repository’s CI workflow builds every pull request and every push to `main`. A tagged release is built by the release workflow when a semantic version tag is pushed:

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

The generated release contains the application image, bootloader, partition table, OTA data image, partition CSV, README, and SHA-256 checksums. Verify that the target board uses the same flash size and partition assumptions before flashing a release artifact.

## 11. Troubleshooting

### No serial output

Confirm the USB cable supports data, the correct serial port is selected, the board is powered, and the terminal is configured for 115200 baud. Try bootloader mode to separate USB/boot issues from application issues. Confirm that no external circuit is holding a boot strap at the wrong level.

### The board repeatedly resets

Disconnect peripherals and field wiring, inspect supply stability and the 3.3 V rail, and check for brownout or watchdog messages. Confirm that the modem is not being powered through an undersized regulator and that the application is using the correct flash configuration.

### A relay behaves inversely

Do not connect a load while debugging. Check the relay driver’s active polarity and update `relay_active_high` in `main/b2_config.c`. Rebuild, reflash, and verify the safe state at reset before reconnecting any load.

### The OLED, RTC, or ADS1115 is missing

Check the shared I2C pins, pull-ups, supply voltage, device address, and wiring. Test one device at a time. The application logs a warning and continues when these optional peripherals fail to initialize, so a successful boot does not prove that the I2C hardware is correct.

### The modem does not register

First verify `AT` communication, modem power/reset timing, SIM insertion, antenna connections, SIM status, and network coverage. Use `modem` to inspect registration, attachment, and CSQ. The firmware does not currently create a packet-data session, so successful SMS registration is not proof that IP networking is configured.

### SMS commands do not operate a relay

Use the exact command strings, confirm modem registration, inspect the serial log for the received SMS, and verify relay wiring and polarity locally with the console. Remember that the reference implementation does not send acknowledgement SMS messages and does not provide sender authentication.

## 12. Safe shutdown and maintenance

Before removing the USB cable or modem supply, switch relays to the safe state and disconnect hazardous loads. Keep the firmware, board profile, schematic, release artifact checksum, and installation record together. Repeat the relay safe-state test after every hardware revision, firmware update, or configuration change.

## References

[1]: ../docs/hardware-integration.md "Hardware integration and electrical safety notes"

[2]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller announcement"

[3]: ../main/b2_config.c "Example GPIO and peripheral profile"

[4]: ../main/app_main.c "Application initialization and SMS command handling"

[5]: https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/get-started/index.html "Espressif ESP-IDF ESP32-S3 getting started"
