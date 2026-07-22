#pragma once

#include <cstdint>
#include "application/ports/driving/ipollable.h"

class IHeatingStateStore;
class ITimeSource;
class IBurnStatsStore;

/// Tracks burner cycle statistics with cumulative counters.
/// Все счётчики NVS-персистентны — инкапсулирует загрузку/сохранение.
class BurnCycleService : public IPollable {
public:
    BurnCycleService(IHeatingStateStore& state, ITimeSource& time, IBurnStatsStore& store);

    void poll() override;
    void reset();

    // ── Cumulative counters (persisted to NVS) ──────────
    uint32_t cycle_count()        const { return cycle_cnt_; }
    uint32_t burner_seconds()     const { return burner_sec_; }
    uint32_t total_pause_seconds() const { return total_pause_sec_; }

    // ── Inter-session pauses (> 10 min between heating sessions) ──
    uint32_t inter_session_pause_sec() const { return inter_session_pause_sec_; }
    uint32_t inter_session_cnt()       const { return inter_session_cnt_; }

    // ── Modulation pauses (<= 10 min, burner cycling within a session) ──
    uint32_t modulation_pause_sec() const { return modulation_pause_sec_; }
    uint32_t modulation_cnt()       const { return modulation_cnt_; }

    // ── Averages (computed from cumulative counters) ────
    float burner_hours()          const;
    float avg_burn_sec()          const;
    float avg_pause_sec()         const;
    float avg_inter_session_pause_sec() const;
    float avg_modulation_pause_sec()    const;

    // ── NVS persistence ─────────────────────────────────
    void load_from_store();
    void save_to_store();

private:
    IHeatingStateStore& state_;
    ITimeSource&        time_;
    IBurnStatsStore&    store_;

    // Cumulative counters
    uint32_t burner_sec_ = 0;
    uint32_t total_pause_sec_ = 0;
    uint32_t cycle_cnt_ = 0;
    uint32_t inter_session_pause_sec_ = 0;
    uint32_t inter_session_cnt_ = 0;
    uint32_t modulation_pause_sec_ = 0;
    uint32_t modulation_cnt_ = 0;

    // Flame edge detection
    bool     prev_flame_ = false;
    uint32_t flame_on_ms_ = 0;
    uint32_t flame_off_ms_ = 0;

    static constexpr uint32_t INTER_SESSION_THRESHOLD_SEC = 600; // 10 min
};
