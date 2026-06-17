#pragma once
#include "application/ports/driving/ipollable.h"
#include "domain/value_objects/pid_quality_metrics.h"

class IHeatingStateStore;
class ITimeSource;

class FopdtEstimator : public IPollable {
public:
    static constexpr int EVENT_RING_SIZE = 10;

    FopdtEstimator(IHeatingStateStore& state, ITimeSource& time);

    void poll() override;
    void reset();

    // Медианные оценки по последним событиям
    float gain() const;
    float time_constant_heat_sec() const;
    float time_constant_cool_sec() const;
    float dead_time_sec() const;
    float outside_temp_typical() const;

    // Сырой доступ к буферу событий (для web/LLM)
    int event_count() const { return event_count_; }
    const FopdtEvent* events() const { return events_; }
    int event_head() const { return event_head_; }

private:
    IHeatingStateStore& state_;
    ITimeSource&        time_;

    // Кольцевой буфер событий
    FopdtEvent events_[EVENT_RING_SIZE];
    int event_head_ = 0;
    int event_count_ = 0;

    // Конечный автомат
    enum State { IDLE, HEAT_UP, SETTLING, STEADY };
    State fsm_state_ = IDLE;

    // Параметры текущего события
    float event_start_room_ = 0;
    float event_target_ = 0;
    float event_max_room_ = 0;
    float event_heat_rate_ = 0;
    uint32_t event_start_ms_ = 0;
    uint32_t flame_on_ms_ = 0;
    uint32_t first_rise_ms_ = 0;
    uint32_t settle_entry_ms_ = 0;
    int settle_stable_count_ = 0;
    int heat_samples_ = 0;
    float cool_rate_sum_ = 0;
    int cool_samples_ = 0;

    // Предыдущий такт (для edge detection)
    bool prev_flame_ = false;
    bool prev_dhw_ = false;
    float prev_room_ = 0;
    uint32_t prev_ms_ = 0;

    static constexpr float SETTLE_BAND = 0.3f;
    static constexpr int SETTLE_STABLE_N = 5;
    static constexpr float MIN_RISE = 0.1f;

    void finalize_event(float outside, uint32_t now_ms);
    void add_event(const FopdtEvent& e);
    float median(float values[EVENT_RING_SIZE], int count) const;
};
