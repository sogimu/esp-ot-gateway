#pragma once

#include <cstdint>
#include <algorithm>
#include "application/ports/driving/ipollable.h"

class IHeatingStateStore;
class ITimeSource;

/// Tracks burner cycle durations (256-entry ring buffer).
/// Computes median/average burn and pause times, burner hours.
class BurnCycleService : public IPollable {
public:
    static constexpr int RING = 256;

    BurnCycleService(IHeatingStateStore& state, ITimeSource& time);
    ~BurnCycleService();

    void poll() override;
    void reset();

    uint32_t cycle_count()   const { return cycle_cnt_; }
    uint32_t burner_seconds() const { return burner_sec_; }
    float    median_burn()   const;
    float    median_pause()  const;
    float    avg_burn()      const;
    float    avg_pause()     const;
    float    burner_hours()  const { return static_cast<float>(burner_sec_) / 3600.0f; }
    uint16_t* burn_dur_ptr()  { return burn_dur_; }
    uint16_t* pause_dur_ptr() { return pause_dur_; }
    int32_t*  cycle_idx_ptr() { return &cycle_idx_; }
    int32_t*  cycle_total_ptr() { return &cycle_total_; }
    uint32_t* burner_sec_ptr() { return &burner_sec_; }
    uint32_t* cycle_cnt_ptr()  { return &cycle_cnt_; }

private:
    IHeatingStateStore& state_;
    ITimeSource&        time_;

    uint16_t* burn_dur_;   // malloc'd
    uint16_t* pause_dur_;  // malloc'd
    int32_t  cycle_idx_ = 0;
    int32_t  cycle_total_ = 0;
    uint32_t burner_sec_ = 0;
    uint32_t cycle_cnt_ = 0;

    bool     prev_flame_ = false;
    uint32_t flame_on_ms_ = 0;
    uint32_t flame_off_ms_ = 0;
    uint32_t last_tick_ms_ = 0;

    static float median(uint16_t* arr, int count);
    static float average(uint16_t* arr, int count);
};
