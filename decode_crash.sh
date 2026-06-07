#!/bin/bash
# Декодирует адреса стектрейса в имена функций и строки исходного кода.
# Использование:
#   ./decode_crash.sh 0x400818ba 0x40089a71 0x400919cd
#   ./decode_crash.sh bt: 0x400818ba 0x40089a71 0x400919cd
# (можно скопировать всю строку "bt: 0x... 0x... 0x..." из журнала)

ELF="build/esp-ot-gateway.elf"
ADDR2LINE="$HOME/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20230928/xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line"

if [ ! -f "$ELF" ]; then
    echo "Ошибка: $ELF не найден. Запускайте из корня проекта."
    exit 1
fi

if [ ! -f "$ADDR2LINE" ]; then
    # Попробовать найти addr2line через PATH
    ADDR2LINE="xtensa-esp32-elf-addr2line"
fi

# Собрать все hex-адреса из аргументов
ADDRS=""
for arg in "$@"; do
    # Пропустить префикс "bt:" и другие не-адреса
    cleaned="${arg#bt:}"
    if [[ "$cleaned" =~ ^0x[0-9a-fA-F]+$ ]]; then
        ADDRS="$ADDRS $cleaned"
    fi
done

if [ -z "$ADDRS" ]; then
    echo "Использование: $0 <hex-адреса…>"
    echo "Пример: $0 bt: 0x400818ba 0x40089a71 0x400919cd"
    exit 1
fi

echo "=== Стек вызовов (addr2line) ==="
echo "ELF: $ELF"
for addr in $ADDRS; do
    result=$("$ADDR2LINE" -e "$ELF" -f -C -p "$addr" 2>/dev/null)
    if [ -n "$result" ]; then
        echo "  $addr → $result"
    else
        echo "  $addr → (не найдено)"
    fi
done
