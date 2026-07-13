#pragma once

#include <cstdint>
#include <cstdio>

/// OpenTherm fault code formatting utilities.
/// No lookup table — OEM codes are manufacturer-specific and undocumented
/// for Baxi in OpenTherm. We always show the numeric code so the user can
/// look it up in the boiler manual or observe patterns over time.
namespace FaultCodes {

/// Returns a static string describing the OEM fault code.
/// Always includes the numeric code.
inline const char* oem_fault_text(uint8_t code) {
    // Static buffer for codes not covered by the simple cases below.
    // NOT thread-safe, but this project runs single-threaded polling.
    static char buf[32];
    if (code == 0) return "нет ошибки";
    snprintf(buf, sizeof(buf), "код %d", code);
    return buf;
}

/// Build a human-readable Russian description of ASF flags bits into buf.
/// These bits ARE standard OpenTherm — no guessing here.
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
