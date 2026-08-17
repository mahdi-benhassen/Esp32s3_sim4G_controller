# Hardware integration notes

## What is verified from public material

The public KinCony B2 announcement identifies an ESP32-S3-WROOM-1U N16R8 module, 12–24 V DC input, two relay outputs, two isolated dry-contact inputs, four ADS1115 analog inputs, four 1-Wire channels, DS3231 RTC, SSD1306 I2C display, SD/SPI, RS485, Ethernet, Wi-Fi, and SIM7600E 4G support. These are product-level capabilities, not a complete schematic [1].

## What must be confirmed from the target schematic

A production carrier must confirm the exact ESP32-S3 GPIO routing, whether any pins are shared with the module's flash/PSRAM or boot straps, relay driver polarity, input isolation polarity, ADS1115 address and front-end scaling, I2C pull-up voltage, DS3231 backup battery arrangement, SSD1306 address, SD card voltage translation, RS485 DE/RE polarity, SIM7600 UART voltage and flow-control wiring, SIM7600 power-key timing, modem reset polarity, modem supply peak current, antenna connector routing, and Ethernet PHY/MAC interface.

The default profile in `main/b2_config.c` is not a commercial B2 pinout. It is an example profile intended to make the application build and to give a new PCB a single place to declare its routing. Replace it before flashing a real board.

## Suggested electrical validation order

Begin with the low-voltage regulator and verify a stable 3.3 V rail under the ESP32-S3's peak current. Confirm that both relay outputs remain de-energized at reset, bootloader entry, brownout, and firmware crash. Test each isolated dry input with a current-limited source. Scan the I2C bus with the field wiring disconnected and verify addresses for ADS1115, DS3231, and SSD1306. Loop RS485 TX/RX through the transceiver with a known-good termination scheme. Finally, connect the SIM7600 carrier with its own appropriately rated supply, validate `AT`, registration, signal quality, SMS reception, and voice control, and only then enable the application-level 4G data session.

## Safety boundary

The firmware project is not a mains wiring design. Relay contact ratings, protective devices, creepage, clearance, enclosure, grounding, surge immunity, antenna safety, and regulatory compliance require a qualified hardware and electrical review. Do not use the prototype to switch hazardous voltage until the finished PCB, enclosure, and installation have been assessed.

## Reference

[1]: https://www.kincony.com/kincony-b2-smart-controller-released.html "KinCony B2 Smart Controller released"
