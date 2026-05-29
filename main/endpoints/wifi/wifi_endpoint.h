#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"

#include <cstdint>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

class WifiEndpoint {
public:
    WifiEndpoint();
    ~WifiEndpoint();

    bool start();
    void stop();

    bool is_connected() const;

private:
    static WifiEndpoint* s_self;

    EventGroupHandle_t event_group_;
    int                retry_count_;
    bool               started_;
    bool               connected_;

    static void event_handler(void* arg, esp_event_base_t base,
                              int32_t id, void* data);
};