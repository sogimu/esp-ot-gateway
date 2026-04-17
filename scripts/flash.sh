#!/usr/bin/env bash
# flash.sh — прошивка ESP32 и просмотр лога
# Использование:
#   bash scripts/flash.sh              # порт /dev/ttyUSB0
#   bash scripts/flash.sh /dev/ttyUSB1

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
PORT="${1:-/dev/ttyUSB0}"

if [ -z "$IDF_PATH" ]; then
    echo "[!] ESP-IDF не активирован. Выполните:"
    echo "    source ~/esp/esp-idf/export.sh"
    exit 1
fi

if [ ! -f "$ROOT/build/gas_boiler.bin" ]; then
    echo "[!] Прошивка не найдена. Сначала соберите:"
    echo "    bash scripts/build.sh"
    exit 1
fi

echo "=== Прошивка ESP32 на $PORT ==="
cd "$ROOT"

# Прошить и сразу открыть монитор
idf.py -p "$PORT" flash monitor

# Для прошивки без монитора:
# idf.py -p "$PORT" flash
