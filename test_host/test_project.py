#!/usr/bin/env python3
"""Fast CI checks that do not require an ESP32 board or ESP-IDF installation."""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_file(relative: str) -> Path:
    path = ROOT / relative
    if not path.is_file():
        fail(f"missing required file: {relative}")
    return path


def main() -> int:
    required = [
        "CMakeLists.txt",
        "main/CMakeLists.txt",
        "sdkconfig.defaults",
        "partitions.csv",
        "main/app_main.c",
        "main/b2_config.c",
        "main/b2_board.c",
        "main/b2_relay.c",
        "main/b2_inputs.c",
        "main/b2_adc.c",
        "main/b2_rtc.c",
        "main/b2_oled.c",
        "main/b2_modem.c",
        "main/b2_console.c",
        "main/b2_security.c",
        "main/b2_runtime.c",
        "main/b2_eventlog.c",
        "main/b2_rules.c",
        "main/b2_ota.c",
        "main/b2_time.c",
        "main/b2_cellular.c",
        "main/include/b2_cellular.h",
        "main/Kconfig",
        "main/idf_component.yml",
        "main/include/b2_time.h",
        "main/b2_http.c",
        "main/include/b2_security.h",
        "main/include/b2_ota.h",
        "test_host/hil_bringup.py",
    ]
    for relative in required:
        require_file(relative)

    root_cmake = require_file("CMakeLists.txt").read_text(encoding="utf-8")
    if "project(kincony_b2_esp32s3)" not in root_cmake:
        fail("root CMake project name is not kincony_b2_esp32s3")
    if "include($ENV{IDF_PATH}/tools/cmake/project.cmake)" not in root_cmake:
        fail("root CMakeLists.txt does not use the ESP-IDF project build")

    main_cmake = require_file("main/CMakeLists.txt").read_text(encoding="utf-8")
    for source in ("app_main.c", "b2_modem.c", "b2_relay.c", "b2_adc.c", "b2_security.c", "b2_rules.c", "b2_eventlog.c", "b2_ota.c", "b2_time.c", "b2_cellular.c", "esp_https_ota", "esp_https_server", "esp_modem"):
        if source not in main_cmake:
            fail(f"main/CMakeLists.txt does not register {source}")

    sdkconfig = require_file("sdkconfig.defaults").read_text(encoding="utf-8")
    if not re.search(r'CONFIG_IDF_TARGET\s*=\s*"esp32s3"', sdkconfig):
        fail("sdkconfig.defaults does not select ESP32-S3")
    if "CONFIG_B2_CELLULAR_PPP_ENABLED=n" not in sdkconfig:
        fail("cellular PPP must remain explicitly opt-in by default")

    partition_rows = []
    with require_file("partitions.csv").open(newline="", encoding="utf-8") as handle:
        for row in csv.reader(line for line in handle if not line.lstrip().startswith("#")):
            if row and row[0].strip():
                partition_rows.append([column.strip() for column in row])
    names = {row[0] for row in partition_rows}
    if not {"nvs", "nvs_keys", "otadata", "ota_0", "ota_1", "storage"}.issubset(names):
        fail(f"partition table is missing required entries: {sorted(names)}")
    if any("ot a_" in name for name in names):
        fail("partition table contains a malformed OTA label")

    c_sources = "\n".join(str(path.read_text(encoding="utf-8")) for path in (ROOT / "main").glob("*.c"))
    prohibited = re.compile(r"#include\s*[<\"](?:Arduino|WiFi|PubSubClient|HardwareSerial)\.h")
    match = prohibited.search(c_sources)
    if match:
        fail(f"non-native framework dependency detected: {match.group(0)}")

    print("ESP-IDF host validation passed")
    print(f"checked {len(required)} required files and {len(partition_rows)} partition entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
