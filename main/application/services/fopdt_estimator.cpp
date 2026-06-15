#include "application/services/fopdt_estimator.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include <cmath>

FopdtEstimator::FopdtEstimator(IHeatingStateStore& state, ITimeSource& time)
    : state_(state), time_(time) {
    prev_ms_ = static_cast<uint32_t>(time_.monotonic_ms());
}

void FopdtEstimator::reset() {
    fsm_state_ = IDLE;
    event_head_ = 0;
    event_count_ = 0;
    prev_flame_ = false;
    prev_dhw_ = false;
    prev_ms_ = static_cast<uint32_t>(time_.monotonic_ms());
}

void FopdtEstimator::poll() {
    state_.lock_shared();
    float room = state_.get_pid_room_temp();
    float target = state_.get_pid_target_room();
    bool flame = state_.is_flame_on();
    bool dhw = state_.is_dhw_active();
    float outside = state_.get_outside_temp();
    state_.unlock_shared();

    uint32_t now = static_cast<uint32_t>(time_.monotonic_ms());
    float dt = (prev_ms_ > 0) ? (now - prev_ms_) / 1000.0f : 0;

    if (dhw) {
        if (!prev_dhw_) fsm_state_ = IDLE;
        prev_flame_ = flame; prev_dhw_ = dhw; prev_room_ = room; prev_ms_ = now;
        return;
    }

    if (dt < 0.5f) {
        return;
    }

    // Сбор остывания в фоне
    if (!flame && dt > 0) {
        float rate = (prev_room_ - room) / dt;
        if (rate > 0.0001f && room > outside + 1.0f) {
            cool_rate_sum_ += rate;
            cool_samples_++;
        }
    }

    switch (fsm_state_) {
    case IDLE:
        if (flame && !prev_flame_ && room < target - SETTLE_BAND) {
            event_start_room_ = room;
            event_target_ = target;
            event_start_ms_ = now;
            flame_on_ms_ = now;
            first_rise_ms_ = 0;
            event_max_room_ = room;
            event_heat_rate_ = 0;
            heat_samples_ = 0;
            cool_rate_sum_ = 0;
            cool_samples_ = 0;
            fsm_state_ = HEAT_UP;
        }
        break;
    case HEAT_UP:
        if (first_rise_ms_ == 0 && (room - event_start_room_) > MIN_RISE)
            first_rise_ms_ = now;
        if (room > event_max_room_) event_max_room_ = room;
        if (heat_samples_ < 10 && (room - event_start_room_) < 0.25f * (target - event_start_room_)) {
            event_heat_rate_ += (room - prev_room_) / dt;
            heat_samples_++;
        }
        if (!flame && prev_flame_) {
            fsm_state_ = IDLE;
        } else if (room >= target - SETTLE_BAND) {
            settle_entry_ms_ = now;
            settle_stable_count_ = 1;
            fsm_state_ = SETTLING;
        }
        break;
    case SETTLING:
        if (room > event_max_room_) event_max_room_ = room;
        if (fabsf(room - target) <= SETTLE_BAND)
            settle_stable_count_++;
        else
            settle_stable_count_ = 0;
        if (settle_stable_count_ >= SETTLE_STABLE_N) {
            finalize_event(outside, now);
            fsm_state_ = IDLE;
        } else if (room < target - SETTLE_BAND && flame) {
            fsm_state_ = HEAT_UP;
        }
        break;
    default: break;
    }

    prev_flame_ = flame; prev_dhw_ = dhw; prev_room_ = room; prev_ms_ = now;
}

void FopdtEstimator::finalize_event(float outside, uint32_t now_ms) {
    FopdtEvent e = {};
    e.outside_temp = outside;
    e.valid = true;

    // Dead time
    if (first_rise_ms_ > 0 && flame_on_ms_ > 0)
        e.dead_time_sec = (first_rise_ms_ - flame_on_ms_) / 1000.0f;
    else
        e.dead_time_sec = 0;

    float delta_room = event_max_room_ - event_start_room_;
    float driving_force = event_target_ - event_start_room_;

    // Gain
    e.gain = (driving_force > 0.1f) ? delta_room / driving_force : 1.0f;

    // tau_heat
    float avg_rate = (heat_samples_ > 0) ? event_heat_rate_ / heat_samples_ : 0.001f;
    e.tau_heat_sec = (avg_rate > 0.0001f) ? e.gain * driving_force / avg_rate : 3600.0f;
    if (e.tau_heat_sec < 300) e.tau_heat_sec = 300;
    if (e.tau_heat_sec > 36000) e.tau_heat_sec = 36000;

    // tau_cool
    float avg_cool_rate = (cool_samples_ > 0) ? cool_rate_sum_ / cool_samples_ : 0.0005f;
    float delta_cool = event_max_room_ - outside;
    e.tau_cool_sec = (avg_cool_rate > 0.0001f && delta_cool > 1.0f)
                     ? delta_cool / avg_cool_rate : e.tau_heat_sec * 1.5f;
    if (e.tau_cool_sec < 300) e.tau_cool_sec = 300;
    if (e.tau_cool_sec > 72000) e.tau_cool_sec = 72000;

    add_event(e);
}

void FopdtEstimator::add_event(const FopdtEvent& e) {
    events_[event_head_] = e;
    event_head_ = (event_head_ + 1) % EVENT_RING_SIZE;
    if (event_count_ < EVENT_RING_SIZE) event_count_++;
}

float FopdtEstimator::median(float values[EVENT_RING_SIZE], int count) const {
    if (count == 0) return 0;
    float copy[EVENT_RING_SIZE];
    for (int i = 0; i < count; i++) copy[i] = values[i];
    // bubble sort для 10 элементов — достаточно
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (copy[i] > copy[j]) { float t = copy[i]; copy[i] = copy[j]; copy[j] = t; }
    return (count % 2) ? copy[count / 2] : (copy[count / 2 - 1] + copy[count / 2]) / 2.0f;
}

float FopdtEstimator::gain() const {
    float arr[EVENT_RING_SIZE];
    for (int i = 0; i < event_count_; i++) arr[i] = events_[i].gain;
    return median(arr, event_count_);
}

float FopdtEstimator::time_constant_heat_sec() const {
    float arr[EVENT_RING_SIZE];
    for (int i = 0; i < event_count_; i++) arr[i] = events_[i].tau_heat_sec;
    return median(arr, event_count_);
}

float FopdtEstimator::time_constant_cool_sec() const {
    float arr[EVENT_RING_SIZE];
    for (int i = 0; i < event_count_; i++) arr[i] = events_[i].tau_cool_sec;
    return median(arr, event_count_);
}

float FopdtEstimator::dead_time_sec() const {
    float arr[EVENT_RING_SIZE];
    for (int i = 0; i < event_count_; i++) arr[i] = events_[i].dead_time_sec;
    return median(arr, event_count_);
}

float FopdtEstimator::outside_temp_typical() const {
    float arr[EVENT_RING_SIZE];
    for (int i = 0; i < event_count_; i++) arr[i] = events_[i].outside_temp;
    return median(arr, event_count_);
}
