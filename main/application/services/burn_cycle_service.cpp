#include "application/services/burn_cycle_service.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/iburn_stats_store.h"
#include "esp_log.h"

BurnCycleService::BurnCycleService(IHeatingStateStore& state, ITimeSource& time, IBurnStatsStore& store)
    : state_(state), time_(time), store_(store)
{
}

void BurnCycleService::poll()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.monotonic_ms());

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

// ── NVS persistence ────────────────────────────────────────────

void BurnCycleService::load_from_store()
{
    uint32_t bs = 0, tps = 0, cc = 0, ips = 0, ic = 0, mps = 0, mc = 0;
    if (store_.load_burn_stats(bs, tps, cc, ips, ic, mps, mc)) {
        burner_sec_               = bs;
        total_pause_sec_          = tps;
        cycle_cnt_                = cc;
        inter_session_pause_sec_  = ips;
        inter_session_cnt_        = ic;
        modulation_pause_sec_     = mps;
        modulation_cnt_           = mc;
        ESP_LOGI("burn_svc", "NVS: восстановлена burn-статистика (burner_sec=%" PRIu32 ")", bs);
    }
}

void BurnCycleService::save_to_store()
{
    store_.save_burn_stats(burner_sec_, total_pause_sec_, cycle_cnt_,
                           inter_session_pause_sec_, inter_session_cnt_,
                           modulation_pause_sec_, modulation_cnt_);
}
