#!/usr/bin/env bash
# build.sh — сборка прошивки для ESP32 через ESP-IDF
# Запускать из корня проекта: bash scripts/build.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."

# Автоматически активировать ESP-IDF, если ещё не
if [ -z "$IDF_PATH" ]; then
    IDF_SCRIPT=~/esp/esp-idf/export.sh
    if [ -f "$IDF_SCRIPT" ]; then
        echo "[*] Активация ESP-IDF..."
        . "$IDF_SCRIPT" > /dev/null 2>&1
    else
        echo "[!] ESP-IDF не найден ($IDF_SCRIPT)"
        exit 1
    fi
fi

echo "=== Сборка esp-ot-gateway (ESP32) ==="
echo "IDF_PATH: $IDF_PATH"
echo ""

cd "$ROOT"
idf.py build

echo ""
echo "=== Сборка завершена ==="
echo ""
echo "Файлы прошивки:"
ls -lh build/esp-ot-gateway.bin build/esp-ot-gateway.elf 2>/dev/null || true
echo ""
echo "Для прошивки:"
echo "    bash scripts/flash.sh /dev/ttyUSB0"
