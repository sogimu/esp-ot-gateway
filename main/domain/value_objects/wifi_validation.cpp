#include "domain/value_objects/wifi_validation.h"

#include <cstring>

WifiValidationResult validate_ssid(const char* ssid)
{
    if (!ssid || ssid[0] == '\0') {
        return {false, "SSID must not be empty"};
    }
    size_t len = strlen(ssid);
    if (len > 32) {
        return {false, "SSID must be 32 characters or fewer"};
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)ssid[i];
        if (c < 32 || c > 126) {
            return {false, "SSID must contain only printable ASCII characters"};
        }
    }
    return {true, nullptr};
}

WifiValidationResult validate_wifi_password(const char* password)
{
    if (!password) {
        return {false, "Password must not be empty"};
    }
    size_t len = strlen(password);
    if (len < 8) {
        return {false, "Password must be at least 8 characters"};
    }
    if (len > 63) {
        return {false, "Password must be 63 characters or fewer"};
    }
    return {true, nullptr};
}

WifiValidationResult validate_ap_password(const char* password)
{
    if (!password || password[0] == '\0') {
        return {false, "AP password must not be empty"};
    }
    return validate_wifi_password(password);
}
