#pragma once

/// Temperature value object with range validation.
/// Zero ESP-IDF / FreeRTOS dependencies — usable in host tests.
class Temperature {
public:
    enum class Range { CH = 0, DHW = 1, AMBIENT = 2 };

    Temperature() : value_(0), valid_(false) {}
    Temperature(float value, Range range);

    float value() const { return value_; }
    bool  is_valid() const { return valid_; }

    static constexpr float CH_MIN = 20.0f;
    static constexpr float CH_MAX = 80.0f;
    static constexpr float DHW_MIN = 35.0f;
    static constexpr float DHW_MAX = 80.0f;

private:
    float value_;
    bool  valid_;
};
