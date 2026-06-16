#pragma once

// Stub for esp_wifi.h — host tests don't have real WiFi.
// Only the functions/types actually used by code compiled under test/ are stubbed.

#include <cstdint>

using esp_err_t = int32_t;

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA  = 1,
    WIFI_MODE_AP   = 2,
    WIFI_MODE_APSTA = 3,
} wifi_mode_t;

inline esp_err_t esp_wifi_get_mode(wifi_mode_t* mode) {
    *mode = WIFI_MODE_APSTA;  // simulate AP running (Fix 1 uses APSTA)
    return 0;  // ESP_OK
}
