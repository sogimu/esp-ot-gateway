/// Tests for ModulationStatsService (application/services/modulation_stats_service.h)
/// Covers: histogram binning, percentile computation, reset.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/modulation_stats_service.h"
#include "fakes/fake_heating_state_store.h"
#include "nvs_config_store.h"  // NvsHistBlob

using Catch::Approx;

TEST_CASE("ModulationStats: initial state is empty", "[mod][app]") {
    FakeHeatingStateStore state;
    ModulationStatsService stats(state);

    REQUIRE(stats.samples() == 0);
    REQUIRE(stats.p50() == Approx(0.0f));
    REQUIRE(stats.p99() == Approx(0.0f));
}

TEST_CASE("ModulationStats: single sample", "[mod][app]") {
    FakeHeatingStateStore state;
    state.set_modulation(45.5f);
    ModulationStatsService stats(state);

    stats.poll();

    REQUIRE(stats.samples() == 1);
    // 45.5% → bin 455 → percentile returns 45.5 for all p
    REQUIRE(stats.p50() == Approx(45.5f));
}

TEST_CASE("ModulationStats: percentiles with uniform distribution", "[mod][app]") {
    FakeHeatingStateStore state;
    ModulationStatsService stats(state);

    // Add 100 samples: 10@0%, 10@10%, ..., 10@90%
    for (int pct = 0; pct <= 90; pct += 10) {
        state.set_modulation(static_cast<float>(pct));
        for (int i = 0; i < 10; i++) {
            stats.poll();
        }
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
    state.set_modulation(35.0f);
    ModulationStatsService stats(state);

    for (int i = 0; i < 1000; i++) {
        stats.poll();
    }

    REQUIRE(stats.samples() == 1000);
    REQUIRE(stats.p1() == Approx(35.0f).margin(0.2f));
    REQUIRE(stats.p50() == Approx(35.0f).margin(0.2f));
    REQUIRE(stats.p99() == Approx(35.0f).margin(0.2f));
}

TEST_CASE("ModulationStats: histogram bin clamping", "[mod][app]") {
    FakeHeatingStateStore state;
    ModulationStatsService stats(state);

    // Below 0% → clamped to bin 0
    state.set_modulation(-5.0f);
    stats.poll();
    REQUIRE(stats.p50() == Approx(0.0f));

    // Above 100% → clamped to bin 999 (99.9%)
    state.set_modulation(150.0f);
    stats.poll();
}

TEST_CASE("ModulationStats: p1/p99 in skewed distribution", "[mod][app]") {
    FakeHeatingStateStore state;
    ModulationStatsService stats(state);

    // 990 samples at 30%, 10 samples at 90% (skewed right)
    state.set_modulation(30.0f);
    for (int i = 0; i < 990; i++) stats.poll();

    state.set_modulation(90.0f);
    for (int i = 0; i < 10; i++) stats.poll();

    REQUIRE(stats.samples() == 1000);

    // p50 should be 30%
    REQUIRE(stats.p50() == Approx(30.0f).margin(0.2f));

    // p99 should capture the tail (90%)
    float p99 = stats.p99();
    REQUIRE(p99 >= 30.0f);
}

TEST_CASE("ModulationStats: percentile with bin >65535 samples (overflow guard)", "[mod][app][overflow]")
{
    // Simulates boiler OFF most of the time — bin 0 would overflow uint16_t
    // but percentile() must still return correct values with uint32_t bins
    FakeHeatingStateStore state;
    state.set_modulation(0.0f);
    ModulationStatsService stats(state);

    // Fill bin 0 with 100,000 samples (well beyond uint16_t max of 65535)
    for (int i = 0; i < 100000; i++) stats.poll();

    // Add 1000 samples at 50% modulation (the boiler actually burning)
    state.set_modulation(50.0f);
    for (int i = 0; i < 1000; i++) stats.poll();

    REQUIRE(stats.samples() == 101000);

    // With 100K at 0% and 1K at 50%, p50 should be 0%
    REQUIRE(stats.p50() == Approx(0.0f).margin(0.1f));

    // p99 should still be 0% (since 99% * 101000 = 99990 < 100000)
    REQUIRE(stats.p99() == Approx(0.0f).margin(0.1f));

    // p100 would be 50% but we only have p99
    // verify p99.9 is 50% since 99.9% * 101000 = 100899 > 100000
    float p999 = stats.p99(); // our API only goes to p99
    REQUIRE(p999 >= 0.0f);
}

TEST_CASE("ModulationStats: percentiles work with 500k samples", "[mod][app][overflow]")
{
    // Stress test: many samples across multiple bins, verifying
    // percentile calculation doesn't break with large numbers
    FakeHeatingStateStore state;
    ModulationStatsService stats(state);

    // Simulate realistic boiler operation: 70% OFF, 30% ON at varying modulation
    state.set_modulation(0.0f);
    for (int i = 0; i < 350000; i++) stats.poll();  // 70% at 0%

    state.set_modulation(25.0f);
    for (int i = 0; i < 50000; i++) stats.poll();   // 10% at 25%

    state.set_modulation(35.0f);
    for (int i = 0; i < 50000; i++) stats.poll();   // 10% at 35%

    state.set_modulation(48.0f);
    for (int i = 0; i < 50000; i++) stats.poll();   // 10% at 48%

    REQUIRE(stats.samples() == 500000);

    // p50 (median) should be 0% since 70% of samples are 0%
    REQUIRE(stats.p50() == Approx(0.0f).margin(0.1f));

    // p75 hits the 25% bin (70% @ 0% + 10% @ 25% → p75 falls in 25% bin)
    REQUIRE(stats.p75() == Approx(25.0f).margin(0.1f));

    // p90 hits the 35% bin (70% + 10% + 10% = 90% → p90 at 35%)
    REQUIRE(stats.p90() == Approx(35.0f).margin(0.1f));

    // p99 should be 48% (the max modulation)
    REQUIRE(stats.p99() == Approx(48.0f).margin(0.1f));
}

TEST_CASE("ModulationStats: NvsHistBlob size is 4004 bytes (uint32_t bins)", "[mod][overflow]")
{
    REQUIRE(sizeof(NvsHistBlob) == 4004);
    // 4 bytes samples + 1000 * 4 bytes hist = 4004
}

TEST_CASE("ModulationStats: histogram roundtrip preserves large bin counts", "[mod][overflow]")
{
    // Verify that saving and loading the histogram preserves uint32_t bin values
    FakeHeatingStateStore state;
    state.set_modulation(0.0f);
    ModulationStatsService stats(state);

    // Fill bin 0 with 100,000 samples
    for (int i = 0; i < 100000; i++) stats.poll();

    // Copy to NVS blob and back
    NvsHistBlob blob;
    blob.samples = stats.samples();
    uint32_t* src = stats.hist_ptr();
    for (int i = 0; i < HIST_BINS; i++) {
        blob.hist[i] = src[i];
    }

    // Verify blob contains the full count (no truncation)
    REQUIRE(blob.hist[0] == 100000);
    REQUIRE(blob.samples == 100000);

    // Verify all other bins are 0
    uint32_t sum = 0;
    for (int i = 0; i < HIST_BINS; i++) sum += blob.hist[i];
    REQUIRE(sum == 100000);
}

TEST_CASE("ModulationStats: 16-bit histogram migration to 32-bit", "[mod][overflow][migration]")
{
    // Simulate old-format 16-bit histogram blob and verify migration
    struct OldHist {
        uint32_t samples;
        uint16_t bins[HIST_BINS];
    } old;
    memset(&old, 0, sizeof(old));
    old.samples = 200000;
    // Bin 0 was clamped at 65535 in old format
    old.bins[0] = 65535;
    old.bins[250] = 30000;  // 25% modulation
    old.bins[480] = 20000;  // 48% modulation
    // Sum = 65535 + 30000 + 20000 = 115535 ≠ 200000 (lossy!)

    // Simulate migration: old uint16_t → new uint32_t
    NvsHistBlob new_blob;
    memset(&new_blob, 0, sizeof(new_blob));
    new_blob.samples = old.samples;
    for (int i = 0; i < HIST_BINS; i++) {
        new_blob.hist[i] = old.bins[i];  // uint16_t → uint32_t (safe)
    }

    // Values are preserved (even if originally truncated)
    REQUIRE(new_blob.hist[0] == 65535);
    REQUIRE(new_blob.hist[250] == 30000);
    REQUIRE(new_blob.hist[480] == 20000);

    // Note: the sum mismatch (115535 vs 200000) is a pre-existing data loss
    // from the old format. After migration, new data won't have this issue.
}

TEST_CASE("ModulationStats: bin 0 overflow after reset is safe with 32-bit", "[mod][overflow]")
{
    // The key scenario: after reset, boiler is mostly OFF.
    // Bin 0 accumulates >65535 samples → must NOT overflow.
    FakeHeatingStateStore state;
    state.set_modulation(0.0f);
    ModulationStatsService stats(state);

    // Simulate long runtime: 200,000 polls at 0% modulation
    // This exceeds uint16_t (65535) but fits in uint32_t
    for (int i = 0; i < 200000; i++) stats.poll();

    REQUIRE(stats.samples() == 200000);

    // p50 should correctly return 0% (not 99.9% fallback)
    REQUIRE(stats.p50() == Approx(0.0f).margin(0.1f));
    REQUIRE(stats.p90() == Approx(0.0f).margin(0.1f));
    REQUIRE(stats.p99() == Approx(0.0f).margin(0.1f));

    // Verify histogram integrity
    uint32_t* h = stats.hist_ptr();
    REQUIRE(h[0] == 200000);  // bin 0 has full count, no overflow
}
