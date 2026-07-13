#pragma once

#include <stdint.h>

/// Burner modulation percentage.
/// Range 0.0 – 100.0%.
/// Provides bin() for 100-bin histogram (1% resolution).
class Modulation {
public:
    Modulation() = default;
    explicit Modulation(float percent);

    float percent() const { return percent_; }
    bool  is_valid() const { return valid_; }

    /// Histogram bin index 0–99 (1% resolution).
    /// Clamped to valid range.
    int   bin() const;

    static constexpr float MIN_PCT = 0.0f;
    static constexpr float MAX_PCT = 100.0f;

private:
    float percent_ = 0;
    bool  valid_   = true;
};
