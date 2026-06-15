#include "application/services/pid_quality_assessor.h"
#include "application/ports/driven/iheating_state_store.h"
#include <cmath>

PidQualityAssessor::PidQualityAssessor(IHeatingStateStore& state)
    : state_(state)
{
    // ring_ is value-initialized (zeroed) automatically
}

void PidQualityAssessor::capture_sample()
{
    state_.lock_shared();

    float room_temp   = state_.get_pid_room_temp();
    float target_temp = state_.get_pid_target_room();
    bool  flame_on    = state_.is_flame_on();
    bool  dhw_active  = state_.is_dhw_active();
    bool  cycle_locked = state_.get_pid_cycle_locked();
    float outside_temp = state_.get_outside_temp();
    float pid_output   = state_.get_pid_output();
    float ch_sp_min    = state_.get_ch_sp_min();
    float ch_sp_max    = state_.get_ch_sp_max();

    state_.unlock_shared();

    bool clamped = (pid_output <= ch_sp_min) || (pid_output >= ch_sp_max);

    PidQualitySample sample;
    sample.minute_of_day = static_cast<uint16_t>(ring_head_);
    sample.room_temp     = to_fixed16(room_temp);
    sample.target_temp   = to_fixed16(target_temp);
    sample.outside_temp  = to_fixed16(outside_temp);
    sample.flags = 0;
    if (flame_on)     sample.flags |= 0x01;
    if (dhw_active)   sample.flags |= 0x02;
    if (cycle_locked) sample.flags |= 0x04;
    if (clamped)      sample.flags |= 0x08;

    ring_[ring_head_] = sample;
    if (++ring_head_ >= RING_SIZE) ring_head_ = 0;
    if (ring_count_ < RING_SIZE) ring_count_++;
}

void PidQualityAssessor::recompute_scores()
{
    if (ring_count_ == 0) {
        scores_ = QualityScores{};
        return;
    }

    // ── Overshoot Score ──────────────────────────────────────
    float max_overshoot = 0.0f;
    for (int i = 0; i < ring_count_; i++) {
        if (!sample_has_dhw(ring_[i])) {
            float room   = from_fixed16(ring_[i].room_temp);
            float target = from_fixed16(ring_[i].target_temp);
            float overshoot = room - target;
            if (overshoot > max_overshoot) max_overshoot = overshoot;
        }
    }
    float overshoot_score = 100.0f - 50.0f * (max_overshoot - 0.3f);
    if (overshoot_score < 0.0f)   overshoot_score = 0.0f;
    if (overshoot_score > 100.0f) overshoot_score = 100.0f;

    // ── Steady-State Score (RMSE) ────────────────────────────
    float sum_sq = 0.0f;
    int   count  = 0;
    for (int i = 0; i < ring_count_; i++) {
        if (!sample_has_dhw(ring_[i]) && !sample_is_locked(ring_[i])) {
            float error = from_fixed16(ring_[i].room_temp)
                        - from_fixed16(ring_[i].target_temp);
            if (fabsf(error) < 0.5f) {
                sum_sq += error * error;
                count++;
            }
        }
    }
    float rmse = (count > 0) ? sqrtf(sum_sq / static_cast<float>(count)) : 0.5f;
    float steady_state_score = 100.0f - 200.0f * rmse;
    if (steady_state_score < 0.0f)   steady_state_score = 0.0f;
    if (steady_state_score > 100.0f) steady_state_score = 100.0f;

    // ── Stability Score (oscillation crossings) ──────────────
    int crossings = 0;
    for (int i = 0; i < ring_count_ - 1; i++) {
        if (!sample_has_dhw(ring_[i]) && !sample_has_dhw(ring_[i + 1])) {
            float err_i    = from_fixed16(ring_[i].room_temp)
                           - from_fixed16(ring_[i].target_temp);
            float err_next = from_fixed16(ring_[i + 1].room_temp)
                           - from_fixed16(ring_[i + 1].target_temp);
            // sign differs and neither is zero
            if ((err_i > 0 && err_next < 0) || (err_i < 0 && err_next > 0)) {
                crossings++;
            }
        }
    }
    float hours        = static_cast<float>(ring_count_) / 60.0f;
    float osc_per_hour = (hours > 0) ? static_cast<float>(crossings) / hours : 0.0f;
    float stability_score = 100.0f - 15.0f * osc_per_hour;
    if (stability_score < 0.0f)   stability_score = 0.0f;
    if (stability_score > 100.0f) stability_score = 100.0f;

    // ── Cycling Score ────────────────────────────────────────
    int flame_samples = 0;
    int total_samples = 0;
    for (int i = 0; i < ring_count_; i++) {
        if (!sample_has_dhw(ring_[i])) {
            total_samples++;
            if (sample_has_flame(ring_[i])) flame_samples++;
        }
    }
    float flame_ratio  = (total_samples > 0)
                             ? static_cast<float>(flame_samples)
                               / static_cast<float>(total_samples)
                             : 0.0f;
    float cycling_score = 100.0f - 200.0f * fabsf(flame_ratio - 0.5f);
    if (cycling_score < 0.0f)   cycling_score = 0.0f;
    if (cycling_score > 100.0f) cycling_score = 100.0f;

    // ── Clamp Score ──────────────────────────────────────────
    int clamp_count = 0;
    for (int i = 0; i < ring_count_; i++) {
        if (sample_is_clamped(ring_[i])) clamp_count++;
    }
    float clamp_ratio = static_cast<float>(clamp_count)
                      / static_cast<float>(ring_count_);
    float clamp_score = 100.0f - 500.0f * clamp_ratio;
    if (clamp_score < 0.0f)   clamp_score = 0.0f;
    if (clamp_score > 100.0f) clamp_score = 100.0f;

    // ── Composite Score ──────────────────────────────────────
    float composite = 0.25f * overshoot_score
                    + 0.25f * steady_state_score
                    + 0.20f * stability_score
                    + 0.15f * cycling_score
                    + 0.15f * clamp_score;
    if (composite < 0.0f)   composite = 0.0f;
    if (composite > 100.0f) composite = 100.0f;

    scores_.overshoot    = overshoot_score;
    scores_.steady_state = steady_state_score;
    scores_.stability    = stability_score;
    scores_.cycling      = cycling_score;
    scores_.clamp        = clamp_score;
    scores_.composite    = composite;
}

void PidQualityAssessor::poll()
{
    poll_tick_++;
    if (poll_tick_ >= 55) {
        poll_tick_ = 0;
        capture_sample();
        static int capture_count = 0;
        capture_count++;
        if (capture_count >= 10) {
            capture_count = 0;
            recompute_scores();
        }
    }
}

void PidQualityAssessor::reset()
{
    ring_head_ = 0;
    ring_count_ = 0;
    poll_tick_ = 0;
    scores_ = QualityScores{};
}
