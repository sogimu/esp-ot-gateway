#pragma once

#include <cstdint>
#include "application/ports/driving/ipollable.h"

class IHeatingStateStore;

/// Tracks modulation histogram (1000 bins) — heap-allocated to avoid stack overflow.
///
/// Thread safety: poll() is called ONLY from main_poll task (single writer).
/// Accessors are called from HTTP task (reader). ESP32 word-aligned reads of
/// uint32_t/float are atomic. hist_[] entries may lag 1-2 poll cycles behind
/// samples_ counter — acceptable for dashboard display.
class ModulationStatsService : public IPollable {
public:
    static constexpr int BINS = 1000;

    ModulationStatsService(IHeatingStateStore& state);
    ~ModulationStatsService();

    void poll() override;
    void reset();

    uint32_t samples() const { return samples_; }
    const uint32_t* hist() const { return hist_; }
    float p1()  const;
    float p10() const;
    float p25() const;
    float p50() const;
    float p75() const;
    float p90() const;
    float p99() const;
    uint32_t* samples_ptr() { return &samples_; }
    uint32_t* hist_ptr() { return hist_; }

private:
    IHeatingStateStore& state_;
    uint32_t* hist_; // malloc'd
    uint32_t samples_ = 0;
    float percentile(float p) const;
};
