#pragma once
#include <cstdint>
#include "application/ports/driving/ipollable.h"
#include "domain/value_objects/pid_quality_metrics.h"

class IHeatingStateStore;

class PidQualityAssessor : public IPollable {
public:
    static constexpr int RING_SIZE = 1440;  // 24h x 60min

    explicit PidQualityAssessor(IHeatingStateStore& state);

    void poll() override;
    void reset();

    const QualityScores& scores() const { return scores_; }
    const PidQualitySample* ring() const { return ring_; }
    int ring_count() const { return ring_count_; }
    int ring_head() const { return ring_head_; }

private:
    void capture_sample();
    void recompute_scores();

    IHeatingStateStore& state_;
    PidQualitySample ring_[RING_SIZE];
    int ring_head_ = 0;
    int ring_count_ = 0;
    uint8_t last_minute_ = 255;
    int poll_tick_ = 0;

    QualityScores scores_;
};
