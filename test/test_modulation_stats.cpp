/// Tests for ModulationStatsService (application/services/modulation_stats_service.h)
/// Covers: histogram binning, percentile computation, reset.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/modulation_stats_service.h"
#include "fakes/fake_heating_state_store.h"

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
