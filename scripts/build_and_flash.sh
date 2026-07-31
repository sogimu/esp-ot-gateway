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

echo "=== 2. Host-тесты ==="
cd "$ROOT"
rm -rf build_tests
mkdir build_tests && cd build_tests
cmake ../test -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1
cmake --build . -j"$(nproc)" 2>&1 | tail -3
echo "Запуск тестов..."
./run_tests 2>&1 | tail -5
cd "$ROOT"

echo "=== 3. Сборка прошивки ==="
touch "$ROOT/main/main.cpp"
# Always fullclean to avoid stale LWIP/sdkconfig cache
idf.py fullclean > /dev/null 2>&1
idf.py build

echo "=== 4. Освобождение порта ==="
fuser -k "$PORT" 2>/dev/null || true
sleep 1

echo "=== 5. Прошивка ==="
# idf.py flash берёт flash size/mode/freq из sdkconfig (а не хардкодит 2MB)
# и дополнительно пишет ota_data_initial.bin (в отличие от ручного esptool).
idf.py -p "$PORT" flash

echo "=== 6. Проверка ==="
sleep 3
python3 -c "
import serial, time
ser = serial.Serial('$PORT', 115200, timeout=1)
start = time.time()
while time.time() - start < 75:
    line = ser.readline()
    if line:
        s = line.decode('utf-8','ignore')
        if 'compile time' in s:
            print('BUILD:', s.strip())
        if 'куча' in s:
            print('HEAP:', s.strip())
            break
ser.close()
"

echo "=== Готово ==="
