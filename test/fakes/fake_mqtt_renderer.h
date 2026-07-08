#pragma once

#include "application/ports/driven/imqtt_state_renderer.h"
#include <cstdio>
#include <cstring>

/// Fake-реализация IMqttStateRenderer для тестов.
class FakeMqttRenderer : public IMqttStateRenderer {
public:
    int render_status(char* buf, size_t size) override {
        render_status_called_++;
        return snprintf(buf, size, "{\"ch_temp\":45.0,\"ch_enable\":1}");
    }

    int render_stats(char* buf, size_t size) override {
        render_stats_called_++;
        return snprintf(buf, size, "{\"samples\":1000}");
    }

    int render_status_called_ = 0;
    int render_stats_called_ = 0;
};
