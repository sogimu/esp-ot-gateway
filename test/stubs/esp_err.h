#pragma once

#include <cstdint>

using esp_err_t = int32_t;

inline const char* esp_err_to_name(esp_err_t code) {
    if (code == 0) return "ESP_OK";
    return "ESP_ERR_***";
}

#define ESP_OK                    0
#define ESP_FAIL                  -1
#define ESP_ERR_OTA_BASE          0x3000
#define ESP_ERR_OTA_ROLLBACK_INVALID_STATE (ESP_ERR_OTA_BASE + 0x10)
