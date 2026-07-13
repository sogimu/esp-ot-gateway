#pragma once

#include <cstdint>
#include <cstdio>

/// OpenTherm ASF (Application Status Flags) decoding.
/// These bits are standard OpenTherm — universal across all boiler brands.
/// OEM fault codes are manufacturer-specific and intentionally NOT decoded here.
namespace FaultCodes {

/// Build a human-readable Russian description of ASF flags bits into buf.
/// Returns the number of characters written (excluding null terminator).
inline int asf_flags_text(uint8_t flags, char* buf, size_t size) {
    if (flags == 0) return snprintf(buf, size, "нет флагов");

    int pos = 0;
    struct { uint8_t bit; const char* name; } bits[] = {
        {0, "сервис"},
        {1, "блокировка"},
        {2, "низкое давление воды"},
        {3, "ошибка газа/пламени"},
        {4, "ошибка давления воздуха"},
        {5, "перегрев"},
        {6, "резерв (bit6)"},
        {7, "резерв (bit7)"},
    };

    for (auto& b : bits) {
        if (flags & (1 << b.bit)) {
            if (pos > 0 && pos < (int)size - 1) {
                pos += snprintf(buf + pos, size - pos, ", ");
            }
            if (pos < (int)size) {
                pos += snprintf(buf + pos, size - pos, "%s", b.name);
            }
        }
    }
    return pos;
}

} // namespace FaultCodes
