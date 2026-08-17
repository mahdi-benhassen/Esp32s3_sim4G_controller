#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF="${IDF_PATH:-/home/ubuntu/esp-idf}"
BUILD_DIR="${ROOT}/test_host/.build"
mkdir -p "${BUILD_DIR}"
SDKCONFIG_DIR="${ROOT}/build/config"
mkdir -p "${SDKCONFIG_DIR}"
if [[ ! -f "${SDKCONFIG_DIR}/sdkconfig.h" ]]; then
  printf '%s\n' '#pragma once' > "${SDKCONFIG_DIR}/sdkconfig.h"
fi

gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -I"${IDF}/components/unity/unity/src" \
  -I"${ROOT}/build/config" \
  -I"${ROOT}/main/include" \
  -I"${IDF}/components/esp_common/include" \
  -I"${IDF}/components/driver/include" \
  -I"${IDF}/components/esp_driver_gpio/include" \
  -I"${IDF}/components/esp_driver_i2c/include" \
  -I"${IDF}/components/esp_driver_spi/include" \
  -I"${IDF}/components/esp_driver_uart/include" \
  -I"${IDF}/components/hal/include" \
  -I"${IDF}/components/soc/esp32s3/include" \
  "${IDF}/components/unity/unity/src/unity.c" \
  "${ROOT}/main/b2_core.c" \
  "${ROOT}/test_host/test_behavior.c" \
  -lm -o "${BUILD_DIR}/test_behavior"

"${BUILD_DIR}/test_behavior"
