#include "application/services/burn_cycle_service.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"

BurnCycleService::BurnCycleService(IHeatingStateStore& state, ITimeSource& time)
    : state_(state), time_(time)
{
}

void BurnCycleService::poll()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.now_us() / 1000);

    state_.lock_shared();
    bool flame = state_.is_flame_on();
    state_.unlock_shared();

    if (flame != prev_flame_) {
        if (flame) {
            // Flame ON: record preceding pause, classify by threshold
            flame_on_ms_ = now_ms;
            if (flame_off_ms_ > 0) {
                uint32_t pause = (now_ms - flame_off_ms_ + 500) / 1000;
                total_pause_sec_ += pause;
                if (pause > INTER_SESSION_THRESHOLD_SEC) {
                    inter_session_pause_sec_ += pause;
                    inter_session_cnt_++;
                } else {
                    modulation_pause_sec_ += pause;
                    modulation_cnt_++;
                }
            }
        } else {
            // Flame OFF: record burn duration
            flame_off_ms_ = now_ms;
            if (flame_on_ms_ > 0) {
                uint32_t burn = (now_ms - flame_on_ms_ + 500) / 1000;
                burner_sec_ += burn;
                cycle_cnt_++;
            }
        }
        prev_flame_ = flame;
    }
}

void BurnCycleService::reset()
{
    burner_sec_ = 0;
    total_pause_sec_ = 0;
    cycle_cnt_ = 0;
    inter_session_pause_sec_ = 0;
    inter_session_cnt_ = 0;
    modulation_pause_sec_ = 0;
    modulation_cnt_ = 0;
}

// ── Averages ────────────────────────────────────────────

float BurnCycleService::burner_hours() const {
    return static_cast<float>(burner_sec_) / 3600.0f;
}

float BurnCycleService::avg_burn_sec() const {
    return cycle_cnt_ ? static_cast<float>(burner_sec_) / cycle_cnt_ : 0;
}

float BurnCycleService::avg_pause_sec() const {
    return cycle_cnt_ ? static_cast<float>(total_pause_sec_) / cycle_cnt_ : 0;
}

float BurnCycleService::avg_inter_session_pause_sec() const {
    return inter_session_cnt_ ? static_cast<float>(inter_session_pause_sec_) / inter_session_cnt_ : 0;
}

float BurnCycleService::avg_modulation_pause_sec() const {
    return modulation_cnt_ ? static_cast<float>(modulation_pause_sec_) / modulation_cnt_ : 0;
}
