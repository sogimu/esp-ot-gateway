#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include <cstdint>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/// Standalone WiFi STA adapter — no dependency on old WifiEndpoint.
class WifiInitAdapter {
public:
    WifiInitAdapter();
    ~WifiInitAdapter();

    bool start();
    bool is_connected() const { return connected_; }

    static constexpr int MAX_RETRY = 10;

private:
    EventGroupHandle_t event_group_ = nullptr;
    int  retry_count_ = 0;
    bool connected_   = false;
    bool started_     = false;
    static WifiInitAdapter* s_self;

    static void event_handler(void* arg, esp_event_base_t base, int32_t id, void* data);
    static const char* TAG;
};
