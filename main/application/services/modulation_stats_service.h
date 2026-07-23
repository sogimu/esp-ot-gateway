#pragma once

#include <cstdint>
#include "application/ports/driving/ipollable.h"

class IHeatingStateStore;
class IHeatingStatsStore;

/// Tracks modulation histogram (100 bins, 1% resolution) — heap-allocated to
/// avoid stack overflow.
///
/// Sampling policy:
///   * Only samples while the burner flame is on — idle time would otherwise
///     flood bin 0 and drag every percentile toward zero.
///   * Exponential aging: once the sample count reaches DECAY_THRESHOLD, every
///     bin is halved. This keeps percentiles responsive to recent operation
///     (a sliding window ~DECAY_THRESHOLD burning-samples wide) and bounds the
///     counters so they never grow without limit across months of runtime.
///
/// Thread safety: poll() is called ONLY from main_poll task (single writer).
/// Accessors are called from HTTP task (reader). ESP32 word-aligned reads of
/// uint32_t/float are atomic. hist_[] entries may lag 1-2 poll cycles behind
/// samples_ counter — acceptable for dashboard display.
class ModulationStatsService : public IPollable {
public:
    static constexpr int BINS = 100;

    /// Histogram bins per 1% of modulation — the single place that encodes
    /// resolution. Bin i covers [i / BINS_PER_PCT, (i+1) / BINS_PER_PCT) percent.
    static constexpr float BINS_PER_PCT = BINS / 100.0f;

    /// Sample count at which the histogram is halved (see decay()).
    /// At ~1.1 s per burning sample this is ~15 h of cumulative burn time
    /// before the first halving, giving an effective window of a few days of
    /// operation — recent enough to react to changes, wide enough to be stable.
    static constexpr uint32_t DECAY_THRESHOLD = 50000;

    ModulationStatsService(IHeatingStateStore& state, IHeatingStatsStore& store);
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
    void load_from_store();
    void fill_histogram(struct NvsHistBlob& blob) const;

private:
    IHeatingStateStore&  state_;
    IHeatingStatsStore&  store_;
    uint32_t* hist_; // malloc'd
    uint32_t samples_ = 0;
    float percentile(float p) const;

    /// Halve every bin and re-derive samples_ from the halved counts, keeping
    /// the counter exactly consistent with the histogram (no drift).
    void decay();
};
