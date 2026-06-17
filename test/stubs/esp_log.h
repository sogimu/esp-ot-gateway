#pragma once

// Stub for ESP-IDF logging — all log calls become no-ops on host.
#define ESP_LOGI(tag, fmt, ...)  ((void)0)
#define ESP_LOGW(tag, fmt, ...)  ((void)0)
#define ESP_LOGE(tag, fmt, ...)  ((void)0)
