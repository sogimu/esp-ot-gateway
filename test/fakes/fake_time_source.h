#pragma once

#include "application/ports/driven/itime_source.h"
#include <cstdint>

/// Fake time source — manually advance time for deterministic tests.
class FakeTimeSource : public ITimeSource {
public:
    FakeTimeSource() : us_(1000000ULL) {}  // start at 1s to avoid zero-time edge cases

    uint64_t now_us() const override { return us_; }
    uint32_t now_sec() const override { return static_cast<uint32_t>(us_ / 1000000ULL); }
    void set_timezone(int) override {}  // no-op in fake

    /// Advance time by given microseconds.
    void advance_us(uint64_t delta_us) { us_ += delta_us; }

    /// Advance by seconds.
    void advance_sec(uint32_t delta_s) { us_ += static_cast<uint64_t>(delta_s) * 1000000ULL; }

    /// Advance by milliseconds.
    void advance_ms(uint32_t delta_ms) { us_ += static_cast<uint64_t>(delta_ms) * 1000ULL; }

    /// Set absolute time in microseconds.
    void set_us(uint64_t t) { us_ = t; }

private:
    uint64_t us_;
};
