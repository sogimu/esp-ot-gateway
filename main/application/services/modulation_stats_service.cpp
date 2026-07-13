#include "application/services/modulation_stats_service.h"
#include "application/ports/driven/iheating_state_store.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

ModulationStatsService::ModulationStatsService(IHeatingStateStore& state)
    : state_(state)
{
    hist_ = static_cast<uint32_t*>(malloc(BINS * sizeof(uint32_t)));
    if (hist_) std::memset(hist_, 0, BINS * sizeof(uint32_t));
}

ModulationStatsService::~ModulationStatsService()
{
    free(hist_);
}

void ModulationStatsService::poll()
{
    state_.lock_shared();
    bool  flame = state_.is_flame_on();
    float mod   = state_.get_modulation();
    state_.unlock_shared();

    // Only sample while the burner is actually firing. When it is off the
    // reported modulation collapses to 0 and would otherwise pile up in bin 0,
    // pulling every percentile down toward zero.
    if (!flame) return;

    int bin = static_cast<int>(mod * BINS_PER_PCT); // 1% resolution
    if (bin < 0) bin = 0;
    if (bin >= BINS) bin = BINS - 1;

    hist_[bin]++;
    samples_++;

    // Age out old data once the window is full so recent operation dominates
    // and the counters stay bounded across months of runtime.
    if (samples_ >= DECAY_THRESHOLD) decay();
}

void ModulationStatsService::decay()
{
    uint32_t remaining = 0;
    for (int i = 0; i < BINS; i++) {
        hist_[i] >>= 1;          // halve — exponential aging of old samples
        remaining += hist_[i];
    }
    samples_ = remaining;        // keep the counter exact, no drift
}

void ModulationStatsService::reset()
{
    for (int i = 0; i < BINS; i++) hist_[i] = 0;
    samples_ = 0;
}

float ModulationStatsService::percentile(float p) const
{
    if (samples_ == 0) return 0;
    if (samples_ == 1) {
        // Single sample: return the bin containing it
        for (int i = 0; i < BINS; i++) {
            if (hist_[i] > 0) return static_cast<float>(i) / BINS_PER_PCT;
        }
        return 0;
    }
    uint32_t target = static_cast<uint32_t>(p * (samples_ - 1));
    uint32_t cum = 0;
    for (int i = 0; i < BINS; i++) {
        cum += hist_[i];
        if (cum > target) return static_cast<float>(i) / BINS_PER_PCT;
    }
    return static_cast<float>(BINS - 1) / BINS_PER_PCT;
}

float ModulationStatsService::p1()  const { return percentile(0.01f); }
float ModulationStatsService::p10() const { return percentile(0.10f); }
float ModulationStatsService::p25() const { return percentile(0.25f); }
float ModulationStatsService::p50() const { return percentile(0.50f); }
float ModulationStatsService::p75() const { return percentile(0.75f); }
float ModulationStatsService::p90() const { return percentile(0.90f); }
float ModulationStatsService::p99() const { return percentile(0.99f); }
