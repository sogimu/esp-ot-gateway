#!/usr/bin/env bash
# build.sh — сборка прошивки для ESP32 через ESP-IDF
# Запускать из корня проекта: bash scripts/build.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."

# Проверить что ESP-IDF активирован
if [ -z "$IDF_PATH" ]; then
    echo "[!] ESP-IDF не активирован. Выполните:"
    echo "    source ~/esp/esp-idf/export.sh"
    exit 1
fi

echo "=== Сборка gas_boiler (ESP32) ==="
echo "IDF_PATH: $IDF_PATH"
echo ""

cd "$ROOT"
idf.py build

echo ""
echo "=== Сборка завершена ==="
echo ""
echo "Файлы прошивки:"
ls -lh build/gas_boiler.bin build/gas_boiler.elf 2>/dev/null || true
echo ""
echo "Для прошивки:"
echo "    bash scripts/flash.sh /dev/ttyUSB0"
