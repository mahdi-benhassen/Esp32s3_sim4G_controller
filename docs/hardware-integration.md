# Hardware integration notes

## Scope and evidence boundary

The public KinCony B2 announcement identifies an ESP32-S3-WROOM-1U N16R8 module, 12–24 V DC input, two relay outputs, two isolated dry-contact inputs, four ADS1115 analog inputs, four 1-Wire channels, DS3231 RTC, SSD1306 I2C display, SD/SPI, RS485, Ethernet, Wi-Fi, and SIM7600E 4G support. These are product-level capabilities, not a complete schematic or electrical certification [1].

This repository implements an independent ESP32-S3 ESP-IDF firmware. The default board profile is an example routing profile, not a commercial B2 pinout. Firmware compilation, host validation, and the optional-feature compile checks prove software integration only; they do not prove that a particular carrier contains the expected transceiver, protection, magnetics, antenna circuit, level shifting, pull-up network, or safe power stage.

## Schematic and board-profile gate

A production carrier must confirm the exact ESP32-S3 GPIO routing, whether any pins are shared with flash/PSRAM or boot straps, relay driver polarity, input isolation polarity, ADS1115 address and front-end scaling, I2C pull-up voltage, DS3231 backup-battery arrangement, SSD1306 address, SD-card voltage translation, RS485 DE/RE polarity, SIM7600 UART voltage and flow-control wiring, SIM7600 power-key timing, modem reset polarity, modem supply peak current, antenna connector routing, and network interface wiring.

The following gates are mandatory before enabling a feature in `main/b2_config.c` or a release-specific `sdkconfig`:

| Area | Required evidence | Release decision |
|---|---|---|
| GPIO and boot straps | Annotated schematic, continuity test, reset/download observation | Reject the profile if a peripheral can force an unintended boot mode or unsafe relay state. |
| Relays and inputs | Driver truth table, isolation test, safe-state measurement at reset/brownout/watchdog | Keep loads disconnected until de-energized startup and fail-safe behavior are recorded. |
| Analog and I2C | ADS1115 address/scaling measurement, pull-up voltage, bus scan, calibration record | Do not use published voltage/current values until the front-end has been calibrated. |
| Modem and RF | SIM7600 variant, supply transient measurement, UART level check, antenna and coax inspection | Do not infer LTE/GNSS performance from a successful `AT` response. |
| Storage and credentials | SD voltage/power validation, filesystem test, physical-access threat decision | Treat SD-held TLS private keys as extractable secrets; see below. |

## Optional W5500 Ethernet boundary

`CONFIG_B2_ETHERNET_ENABLED` is disabled by default. When enabled, the firmware uses the native ESP-IDF W5500 SPI MAC/PHY driver and ESP-NETIF glue with SPI MOSI, MISO, SCLK, CS, optional interrupt, and optional reset pins from the board profile. The temporary opt-in build verifies that the source compiles with the ESP-IDF W5500 symbols; it does not establish that the target board has a W5500, a compatible RJ45/magnetics path, correct interrupt polarity, or usable pin routing.

Before enabling Ethernet on a carrier, document the W5500 part number, SPI host and maximum clock, chip-select timing, reset timing, interrupt/polling choice, magnetics and cable shield strategy, link LEDs, PHY power rail, ESD discharge path, and any shared SPI-bus arbitration. Validate link-up/down recovery, DHCP/static addressing, IPv4 reachability, TLS traffic, sustained throughput, thermal behavior, and operation during Wi-Fi or cellular loss. IPv6 and transport-priority failover are not claimed by this adapter until separately implemented and tested.

## Optional PCA9548A I2C expansion boundary

`CONFIG_B2_I2C_EXPANDER_ENABLED` is disabled by default. When enabled, the firmware provides a small native I2C helper for a PCA9548A-compatible channel selector. Initialization disables all channels; callers must explicitly select a channel before accessing downstream devices and disable the channels after the transaction group. Existing base-bus devices are not automatically migrated behind the expander.

The target schematic must confirm the actual expander component, address straps, reset behavior, pull-up placement and voltage domains, downstream capacitance, channel isolation, and whether any downstream address collisions exist. Acceptance testing must select each channel, probe a known device, verify that deselection isolates it, exercise a stuck-bus recovery path, and confirm that an unpopulated channel cannot disturb the base I2C devices. The helper is an integration foundation, not evidence that the commercial B2 expansion connector uses a PCA9548A.

## SD-card TLS key risk

The HTTPS diagnostics service can load `server.crt` and `server.key` from the SD-card root. This is a deliberate provisioning compromise: the firmware can validate and use the files, but a person who can remove or read the SD card can copy the private key. SD-card access must therefore be treated as equivalent to access to the HTTPS server credential. Do not use a long-lived shared private key across controllers, do not place production keys in Git, and do not treat encrypted NVS as protection for files stored in plaintext on removable media.

For production, provision a unique certificate/key pair per controller, record its fingerprint, restrict physical enclosure access, use a tamper-evident service procedure, rotate or revoke the credential after card replacement, and remove the card after provisioning when the deployment does not require runtime certificate loading. If the threat model requires protection against a hostile person with physical flash/SD access, move key storage to a verified secure element or protected flash design before deployment; the current SD-based path does not provide that guarantee.

## EMC, ESD, surge, and antenna acceptance checklist

The following checklist is a minimum engineering acceptance record, not a claim of regulatory compliance. Test limits and methods must be selected by the responsible hardware engineer for the installation environment and applicable standards.

| Test area | Minimum acceptance evidence |
|---|---|
| Conducted supply noise | Measure ESP32-S3 and SIM7600 rails during boot, relay transitions, LTE attach, transmit bursts, SD writes, and Ethernet link activity. Record minimum voltage, reset/brownout events, and regulator temperature. |
| Relay transients | Test the real driver, suppression network, load class, and wiring length. Verify that switching does not corrupt I2C, RS485, modem operation, storage, or relay safe state. |
| ESD | Apply the approved contact/air discharge test to accessible connectors, enclosure points, buttons, USB, Ethernet, and field terminals with relays in the safe state. Record resets, corrupted settings, link recovery, and permanent damage. |
| Surge and EFT | Use the installation-appropriate conducted surge/electrical-fast-transient plan. Verify protective components, isolation barriers, earth/chassis strategy, and post-test boot/configuration integrity. |
| Radiated susceptibility/emissions | Exercise LTE transmit, Wi-Fi, Ethernet, SD, RS485, ADC sampling, OLED refresh, and relay switching while monitoring resets, false input edges, ADC error, and communication failures. |
| Antenna and RF path | Verify the exact SIM7600 variant, antenna type and connector, coax routing, ground/reference design, mechanical strain relief, isolation from relay wiring, and an approved current/temperature profile during network attach and transmit. Record registration, CSQ/RSRP where available, GNSS fix time, and fallback behavior at representative locations. |
| Recovery and logging | Confirm watchdog/brownout recovery, event-log integrity, NVS wear policy, RTC continuity, and safe relay state after every disturbance. |

Do not connect mains or other hazardous loads during firmware bring-up or EMC troubleshooting. Relay contact ratings, protective devices, creepage, clearance, enclosure, grounding, surge immunity, antenna safety, and regulatory compliance require a qualified hardware and electrical review.

## Suggested validation order

Begin with a visual inspection and continuity check, then validate the low-voltage regulator under the ESP32-S3 and SIM7600 peak load. Confirm that both relay outputs remain de-energized at reset, bootloader entry, brownout, watchdog recovery, and firmware crash. Test each isolated dry input with a current-limited source. Scan the base I2C bus with field wiring disconnected and verify ADS1115, DS3231, and SSD1306 addresses. If the expander is populated, test each PCA9548A channel and its deselection behavior. Validate SD-card mounting and certificate provisioning without production credentials.

Next, loop RS485 TX/RX through the transceiver with the intended termination and biasing. Validate the optional W5500 link and recovery path only after the SPI and Ethernet schematic evidence is complete. Finally, connect the SIM7600 carrier with its own appropriately rated supply, validate `AT`, registration, signal quality, SMS reception, GNSS where supported, and LTE transmit current. Only after these steps should application-level PPP, MQTT, HTTPS, OTA, or unattended relay control be enabled.

## Reference

[1]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller released"


## Production security and field acceptance addendum

The encrypted NVS credential path reduces exposure of HTTPS private keys, but it does not eliminate physical-access risk. Before production release, verify that legacy SD-card `server.key` and `server.crt` files were imported successfully into encrypted NVS and removed. Do not ship a board with private TLS material left on removable media. Treat an SD card from a returned field device as sensitive material until it has been securely erased or physically destroyed under the service policy.

Secure Boot v2 and release flash encryption are manufacturing controls, not ordinary runtime features. Use the reviewed `sdkconfig.production-secure.defaults` profile only with a board-specific signing-key custody procedure. Record the chip identity, bootloader/application hashes, eFuse summary, flash-encryption state, and recovery policy. Validate the exact module and flash configuration before irreversible provisioning; a build artifact alone is not evidence of a correctly locked device.

## HIL evidence package

Attach the JSON report from `test_host/hil_bringup.py`, serial boot log, flashed image hash, board profile revision, power measurements, modem/GNSS results, and the signed physical-gate checklist to the release record. A green CI build plus a serial smoke test is a software gate, not a product certification.

The HIL script reports, but cannot prove, relay contact life under the intended load, EMC/ESD/surge acceptance, antenna and GNSS validation for the installed SIM7600 variant, Secure Boot/flash-encryption provisioning, and BLE client security behavior. These gates require signed bench evidence and are mandatory before field deployment.

## Relay and field-load acceptance

Test relay contact life, inrush, inductive suppression, thermal rise, contact welding behavior, and failure-to-safe behavior with the actual load class and enclosure. Firmware interlock and fail-safe-off are defensive controls; they do not establish contact ratings, arc suppression, fuse coordination, creepage, clearance, or compliance. Keep mains and hazardous loads disconnected until a qualified reviewer signs the electrical acceptance record.
