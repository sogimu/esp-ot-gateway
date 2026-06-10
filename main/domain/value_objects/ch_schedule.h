#pragma once

#include <stdbool.h>

/// 24-hour central-heating setpoint schedule — domain value object.
struct CH_Schedule {
    bool  enabled = false;
    float temps[24] = {};

    bool is_valid() const {
        for (int i = 0; i < 24; i++) {
            if (temps[i] < 20.0f || temps[i] > 80.0f) return false;
        }
        return true;
    }

    float get_for_hour(int hour) const {
        if (hour < 0 || hour >= 24) return 30.0f;
        return temps[hour];
    }
};
