#pragma once

#include <cstring>
#include <cstdlib>

/// Parse a float value for a JSON key using simple strstr scan.
/// Returns -1e38f if key is not found. Suitable for small flat JSON bodies.
inline float json_get_float(const char* json, const char* key) {
    const char* p = strstr(json, key);
    if (!p) return -1e38f;
    p += strlen(key);
    while (*p == ':' || *p == ' ') p++;
    return static_cast<float>(atof(p));
}

/// Parse an int value for a JSON key.
/// Returns -1 if key is not found OR if the value parses to a float < -1e37f.
/// IMPORTANT: -1 is also a valid integer value for timezone offsets.
/// Use json_get_float() + guard (> -1e37f) when -1 is a valid value.
inline int json_get_int(const char* json, const char* key) {
    float v = json_get_float(json, key);
    return (v < -1e37f) ? -1 : static_cast<int>(v);
}
