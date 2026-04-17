#!/usr/bin/env bash
# setup.sh — установка ESP-IDF и зависимостей для gas_boiler
# Запускать из корня проекта: bash scripts/setup.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
IDF_DIR="$HOME/esp/esp-idf"
IDF_VERSION="v5.2.2"    # стабильная версия

echo "=== Установка зависимостей для ESP32 ==="

# ─── Системные пакеты ────────────────────────────────────────────────────────
echo "[*] Установка системных пакетов..."
sudo apt-get update -qq
sudo apt-get install -y \
    git wget flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 usbutils

# ─── ESP-IDF ─────────────────────────────────────────────────────────────────
if [ ! -d "$IDF_DIR" ]; then
    echo "[*] Клонирование ESP-IDF $IDF_VERSION → $IDF_DIR"
    mkdir -p "$HOME/esp"
    git clone --depth 1 --branch "$IDF_VERSION" --recursive \
        https://github.com/espressif/esp-idf.git "$IDF_DIR"
else
    echo "[✓] ESP-IDF уже присутствует: $IDF_DIR"
fi

# ─── Установка тулчейна ESP-IDF ──────────────────────────────────────────────
echo "[*] Установка тулчейна ESP-IDF (может занять несколько минут)..."
"$IDF_DIR/install.sh" esp32

# ─── Права на USB (для прошивки без sudo) ────────────────────────────────────
if ! groups | grep -q dialout; then
    echo "[*] Добавление пользователя в группу dialout..."
    sudo usermod -aG dialout "$USER"
    echo "[!] Выйдите из сессии и войдите снова для применения прав USB"
fi

echo ""
echo "=== Установка завершена ==="
echo ""
echo "Далее:"
echo "  1. Задайте WiFi в main/wifi_config.h"
echo "  2. Активируйте окружение ESP-IDF:"
echo "       source ~/esp/esp-idf/export.sh"
echo "  3. Соберите проект:"
echo "       bash scripts/build.sh"
