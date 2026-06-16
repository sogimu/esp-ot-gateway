#pragma once

#include <cstddef>

/// Result of WiFi credential validation.
struct WifiValidationResult {
    bool ok;
    const char* error;  // human-readable error message (static string, never freed)
};

/// Validate WiFi SSID.
/// Rules: non-empty, printable ASCII (32-126), length 1-32 bytes.
WifiValidationResult validate_ssid(const char* ssid);

/// Validate WiFi password.
/// Rules: length 8-63 characters (WPA2 minimum).
WifiValidationResult validate_wifi_password(const char* password);

/// Validate AP password.
/// Rules: non-empty, length 8-63 characters.
WifiValidationResult validate_ap_password(const char* password);
