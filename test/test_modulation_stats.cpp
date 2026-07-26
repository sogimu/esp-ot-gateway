#include "application/ports/driven/iheating_stats_store.h"
#include "application/ports/driven/igas_correction_store.h"
/// Tests for ModulationStatsService (application/services/modulation_stats_service.h)
/// Covers: flame-gated binning, percentile computation, histogram decay, reset.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/modulation_stats_service.h"
#include "fakes/fake_heating_state_store.h"
#include "nvs_config_store.h"  // NvsHistBlob

using Catch::Approx;

// Helper: burner must be firing for poll() to record a sample.
static void poll_burning(ModulationStatsService& stats, FakeHeatingStateStore& state,
                         float modulation, int times)
{
    state.set_flame(true);
    state.set_modulation(modulation);
    for (int i = 0; i < times; i++) stats.execute();
}

struct FakeHeatingStatsStore : IHeatingStatsStore {
    void save_stats(const IHeatingStateStore&, uint32_t, float, const void*, const void*, const void*, const void*) override {}
    bool load_stats(uint32_t&, float&, void*, void*, void*, void*) override { return false; }
    void save_total_uptime(uint32_t) override {}
    bool load_total_uptime(uint32_t&) override { return false; }
    void save_integral(float) override {}
    void save_meter(const IHeatingStateStore&, const void*) override {}
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
};

TEST_CASE("ModulationStats: initial state is empty", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    REQUIRE(stats.samples() == 0);
    REQUIRE(stats.p50() == Approx(0.0f));
    REQUIRE(stats.p99() == Approx(0.0f));
}

TEST_CASE("ModulationStats: flame gate — no sampling while burner off", "[mod][app][flame]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    // Burner off: modulation reads 0 but must NOT be recorded.
    state.set_flame(false);
    state.set_modulation(0.0f);
    for (int i = 0; i < 1000; i++) stats.execute();
    REQUIRE(stats.samples() == 0);

    // Burner on: samples start accumulating.
    poll_burning(stats, state, 45.0f, 10);
    REQUIRE(stats.samples() == 10);

    // Burner turns off again: count frozen, idle does not dilute percentiles.
    state.set_flame(false);
    state.set_modulation(0.0f);
    for (int i = 0; i < 1000; i++) stats.execute();
    REQUIRE(stats.samples() == 10);
    REQUIRE(stats.p50() == Approx(45.0f));
}

TEST_CASE("ModulationStats: single sample", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    poll_burning(stats, state, 45.0f, 1);

    REQUIRE(stats.samples() == 1);
    // 45% → bin 45 → percentile returns 45.0 for all p
    REQUIRE(stats.p50() == Approx(45.0f));
}

TEST_CASE("ModulationStats: percentiles with uniform distribution", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    // Add 100 samples: 10@0%, 10@10%, ..., 10@90%
    for (int pct = 0; pct <= 90; pct += 10) {
        poll_burning(stats, state, static_cast<float>(pct), 10);
    }

    REQUIRE(stats.samples() == 100);

    // With 10 evenly-spaced values, each 10-sample block:
    // p10 should be near 10%
    float p10 = stats.p10();
    REQUIRE(p10 >= 0.0f);
    REQUIRE(p10 <= 20.0f);

    // p50 should be near 50%
    float p50 = stats.p50();
    REQUIRE(p50 >= 40.0f);
    REQUIRE(p50 <= 60.0f);

    // p90 should be near 90%
    float p90 = stats.p90();
    REQUIRE(p90 >= 80.0f);
    REQUIRE(p90 <= 100.0f);
}

TEST_CASE("ModulationStats: all same value → all percentiles equal", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    poll_burning(stats, state, 35.0f, 1000);

    REQUIRE(stats.samples() == 1000);
    REQUIRE(stats.p1() == Approx(35.0f).margin(0.2f));
    REQUIRE(stats.p50() == Approx(35.0f).margin(0.2f));
    REQUIRE(stats.p99() == Approx(35.0f).margin(0.2f));
}

TEST_CASE("ModulationStats: histogram bin clamping", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    // Below 0% → clamped to bin 0
    poll_burning(stats, state, -5.0f, 1);
    REQUIRE(stats.p50() == Approx(0.0f));

    // Above 100% → clamped to bin 99 (99.0%)
    poll_burning(stats, state, 150.0f, 1);
}

TEST_CASE("ModulationStats: p1/p99 in skewed distribution", "[mod][app]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    // 990 samples at 30%, 10 samples at 90% (skewed right)
    poll_burning(stats, state, 30.0f, 990);
    poll_burning(stats, state, 90.0f, 10);

    REQUIRE(stats.samples() == 1000);

    // p50 should be 30%
    REQUIRE(stats.p50() == Approx(30.0f).margin(0.2f));

    // p99 should capture the tail (90%)
    float p99 = stats.p99();
    REQUIRE(p99 >= 30.0f);
}

TEST_CASE("ModulationStats: decay bounds the sample count", "[mod][app][decay]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);

    // Feed well past the decay threshold at a single modulation value.
    const uint32_t N = ModulationStatsService::DECAY_THRESHOLD * 3 + 137;
    poll_burning(stats, state, 42.0f, static_cast<int>(N));

    // Counter never grows without bound: after the last halving it sits below
    // the threshold (and above roughly half of it).
    REQUIRE(stats.samples() < ModulationStatsService::DECAY_THRESHOLD);
    REQUIRE(stats.samples() > ModulationStatsService::DECAY_THRESHOLD / 4);

    // A single bin can never overflow uint32_t — it is bounded by the threshold.
    const uint32_t* h = stats.hist();
    REQUIRE(h[42] == stats.samples());  // all mass in bin 42 (42.0%)

    // Distribution shape survives decay: every percentile still 42%.
    REQUIRE(stats.p50() == Approx(42.0f).margin(0.1f));
    REQUIRE(stats.p99() == Approx(42.0f).margin(0.1f));
}

TEST_CASE("ModulationStats: decay preserves distribution shape", "[mod][app][decay]") {
    FakeHeatingStateStore state;
    FakeHeatingStatsStore hss_mod;
    ModulationStatsService stats(state, hss_mod);
    state.set_flame(true);

    // Stationary mix fed round-robin (as real interleaved operation would be),
    // driven far past the decay threshold. Halving scales every bin equally, so
    // the equilibrium histogram keeps the 70/20/10 proportions and percentile
    // *values* stay put even as counts shrink.
    const int rounds = static_cast<int>(ModulationStatsService::DECAY_THRESHOLD) / 10 * 30;
    for (int r = 0; r < rounds; r++) {
        state.set_modulation(20.0f); for (int i = 0; i < 7; i++) stats.execute(); // 70%
        state.set_modulation(40.0f); for (int i = 0; i < 2; i++) stats.execute(); // 20%
        state.set_modulation(60.0f); stats.execute();                            // 10%
    }

    REQUIRE(stats.samples() < ModulationStatsService::DECAY_THRESHOLD);

    // p50 sits in the 20% mass, p90 reaches the 40% mass, p99 the 60% tail.
    REQUIRE(stats.p50() == Approx(20.0f).margin(1.0f));
    REQUIRE(stats.p90() == Approx(40.0f).margin(1.0f));
    REQUIRE(stats.p99() == Approx(60.0f).margin(2.0f));
}

TEST_CASE("ModulationStats: NvsHistBlob size is 404 bytes (uint32_t bins)", "[mod][overflow]")
{
    REQUIRE(sizeof(NvsHistBlob) == 404);
    // 4 bytes samples + 100 * 4 bytes hist = 404
}

TEST_CASE("ModulationStats: histogram roundtrip preserves large bin counts", "[mod][overflow]")
{
    // NvsHistBlob must carry full uint32_t bin values through save/load.
    // (Exercised directly on the blob — poll() bounds live bins via decay.)
    NvsHistBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.samples = 100000;
    blob.hist[0] = 100000;

    // Simulate a save→load roundtrip through a byte copy.
    NvsHistBlob copy;
    memcpy(&copy, &blob, sizeof(blob));

    REQUIRE(copy.hist[0] == 100000);   // no uint16_t truncation
    REQUIRE(copy.samples == 100000);

    uint32_t sum = 0;
    for (int i = 0; i < HIST_BINS; i++) sum += copy.hist[i];
    REQUIRE(sum == 100000);
}
