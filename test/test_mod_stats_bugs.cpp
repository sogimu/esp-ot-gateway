#include <catch2/catch_test_macros.hpp>
#include "application/services/modulation_stats_service.h"
#include "fakes/fake_heating_state_store.h"

// ═══════════════════════════════════════════════════════════════
// ModulationStatsService — percentile and histogram bugs
// ═══════════════════════════════════════════════════════════════

// Burner must be firing for poll() to record a sample (flame gate).
static void poll_burning(ModulationStatsService& svc, FakeHeatingStateStore& state,
                         float modulation, int times)
{
    state.set_flame(true);
    state.set_modulation(modulation);
    for (int i = 0; i < times; i++) svc.poll();
}

TEST_CASE("ModulationStats: percentile boundary condition", "[mod_stats]")
{
    // FIXED: percentile() now uses `cum >= target` for correct boundary behavior.

    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    // 50% at 3.0% modulation, 50% at 7.0%
    poll_burning(svc, state, 3.0f, 50);
    poll_burning(svc, state, 7.0f, 50);

    REQUIRE(svc.samples() == 100);

    // With cum >= target: p50 should return 3.0 (the median value)
    // First 50 samples are at 3.0%, then 50 at 7.0%
    float p50 = svc.p50();
    INFO("p50=" << p50);
    CHECK(p50 == 3.0f);
}

TEST_CASE("ModulationStats: percentile with single sample", "[mod_stats]")
{
    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    poll_burning(svc, state, 5.0f, 1);  // bin 5

    REQUIRE(svc.samples() == 1);

    // All percentiles should return the same value for single sample
    float p1  = svc.p1();
    float p50 = svc.p50();
    float p99 = svc.p99();

    CHECK(p1 == 5.0f);
    CHECK(p50 == 5.0f);
    CHECK(p99 == 5.0f);
}

TEST_CASE("ModulationStats: percentile clamping", "[mod_stats]")
{
    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    // Modulation at 0%
    poll_burning(svc, state, 0.0f, 1);

    // Modulation at 100% → bin 100, clamped to BINS-1=99 → 99.0%
    poll_burning(svc, state, 100.0f, 1);

    REQUIRE(svc.samples() == 2);

    float p1  = svc.p1();
    float p99 = svc.p99();

    INFO("p1=" << p1 << " p99=" << p99);
    CHECK(p1 >= 0.0f);
    CHECK(p99 <= 99.0f);
}

TEST_CASE("ModulationStats: percentile on empty data", "[mod_stats]")
{
    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    REQUIRE(svc.samples() == 0);

    // All percentiles should return 0 when no samples
    CHECK(svc.p1()  == 0.0f);
    CHECK(svc.p10() == 0.0f);
    CHECK(svc.p50() == 0.0f);
    CHECK(svc.p99() == 0.0f);
}

TEST_CASE("ModulationStats: histogram bin edges", "[mod_stats]")
{
    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    // Exact bin boundaries: mod=1.0% → bin=1, mod=0.0% → bin=0
    poll_burning(svc, state, 0.0f, 1);
    poll_burning(svc, state, 99.0f, 1);

    REQUIRE(svc.samples() == 2);

    float p50 = svc.p50();
    INFO("p50=" << p50);
    CHECK(p50 >= 0.0f);
}

TEST_CASE("ModulationStats: monotonic percentiles", "[mod_stats]")
{
    // Percentiles must be non-decreasing: p1 ≤ p10 ≤ p25 ≤ p50 ≤ p75 ≤ p90 ≤ p99
    FakeHeatingStateStore state;
    ModulationStatsService svc(state);

    // Random-ish modulation values
    float vals[] = {3.0f, 7.0f, 12.0f, 4.5f, 8.0f, 2.0f, 9.5f, 1.0f,
                    15.0f, 5.0f, 11.0f, 6.0f, 10.0f, 0.5f, 13.0f, 7.5f};
    for (float v : vals) {
        poll_burning(svc, state, v, 1);
    }

    float p1  = svc.p1();
    float p10 = svc.p10();
    float p25 = svc.p25();
    float p50 = svc.p50();
    float p75 = svc.p75();
    float p90 = svc.p90();
    float p99 = svc.p99();

    INFO("p1=" << p1 << " p10=" << p10 << " p25=" << p25 << " p50="
         << p50 << " p75=" << p75 << " p90=" << p90 << " p99=" << p99);

    CHECK(p1  <= p10);
    CHECK(p10 <= p25);
    CHECK(p25 <= p50);
    CHECK(p50 <= p75);
    CHECK(p75 <= p90);
    CHECK(p90 <= p99);
}
