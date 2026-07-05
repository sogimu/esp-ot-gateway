#!/usr/bin/env bash
# build_and_flash.sh — надёжная сборка и прошивка esp-ot-gateway
# Использование: bash scripts/build_and_flash.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-/dev/ttyUSB0}"
IDF_EXPORT="$HOME/esp-idf/esp-idf/export.sh"

echo "=== 1. Активация ESP-IDF v5.3.2 ==="
source "$IDF_EXPORT" > /dev/null 2>&1
echo "IDF: $(idf.py --version 2>/dev/null || echo 'v5.3.2')"

echo "=== 2. Сборка ==="
cd "$ROOT"
# Force recompile of main.cpp to update __DATE__/__TIME__ in log
touch "$ROOT/main/main.cpp"
# Fullclean if sdkconfig or sdkconfig.defaults changed
if [ "$ROOT/sdkconfig.defaults" -nt "$ROOT/build/sdkconfig" ] || [ "$ROOT/sdkconfig" -nt "$ROOT/build/sdkconfig" ]; then
    echo "sdkconfig изменён → fullclean"
    idf.py fullclean
fi
idf.py build

echo "=== 3. Освобождение порта ==="
fuser -k "$PORT" 2>/dev/null || true
sleep 1

echo "=== 4. Прошивка ==="
python3 -m esptool \
    --chip esp32 \
    -p "$PORT" \
    -b 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_size 2MB \
    --flash_freq 80m \
    0x1000  build/bootloader/bootloader.bin \
    0x8000  build/partition_table/partition-table.bin \
    0x10000 build/esp-ot-gateway.bin

echo "=== 5. Проверка ==="
sleep 3
python3 -c "
import serial, time
ser = serial.Serial('$PORT', 115200, timeout=1)
start = time.time()
while time.time() - start < 75:
    line = ser.readline()
    if line:
        s = line.decode('utf-8','')
        if 'compile time' in s:
            print('BUILD:', s.strip())
        if 'куча' in s:
            print('HEAP:', s.strip())
            break
ser.close()
"

echo "=== Готово ==="
