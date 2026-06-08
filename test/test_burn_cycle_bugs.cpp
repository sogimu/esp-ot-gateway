#include <catch2/catch_test_macros.hpp>
#include "application/services/burn_cycle_service.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

// ═══════════════════════════════════════════════════════════════
// BurnCycleService — known bugs verification
// ═══════════════════════════════════════════════════════════════

TEST_CASE("BurnCycleService: burner_sec_ no longer double-counted", "[burn_cycle]")
{
    // FIXED: burner_sec_ now only accumulates on flame-off edge.
    // Ongoing accumulation removed — no more double counting.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Flame comes on at t=60s
    time.advance_ms(60000);
    state.set_flame(true);
    svc.poll();

    // Burn for 30 seconds (3 polls)
    for (int i = 0; i < 3; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    // Flame goes off
    time.advance_ms(1);
    state.set_flame(false);
    svc.poll();

    // Actual burn: ~30 seconds → ~0.0083 hours
    float hours = svc.burner_hours();
    float expected = 30.0f / 3600.0f;

    INFO("burner_hours=" << hours);
    CHECK(hours > 0.0f);
    CHECK(hours < 0.02f); // must be less than 72s (would be ~60s with double-count)
    CHECK(std::abs(hours - expected) < 0.003f);
}

TEST_CASE("BurnCycleService: reset clears all data", "[burn_cycle]")
{
    // FIXED: reset() now uses RING * sizeof(uint16_t) instead of pointer size.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Generate a burn cycle
    time.advance_ms(10000);
    state.set_flame(true);
    svc.poll();
    time.advance_ms(30000);
    state.set_flame(false);
    svc.poll();
    time.advance_ms(100000);
    state.set_flame(true);
    svc.poll();

    int before = svc.cycle_count();

    svc.reset();

    int after = svc.cycle_count();
    float after_avg = svc.avg_burn();

    CHECK(after == 0);
    CHECK(after_avg == 0.0f);
}

TEST_CASE("BurnCycleService: avg_burn uses cycle_cnt_ correctly", "[burn_cycle]")
{
    // FIXED: avg_burn() and median_burn() now use cycle_cnt_ (burns),
    // not cycle_total_ (pauses).

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Cycle 1: burn 50s, pause 100s
    state.set_flame(true);
    svc.poll();
    time.advance_ms(50000);
    state.set_flame(false);
    svc.poll();
    time.advance_ms(100000);
    state.set_flame(true);
    svc.poll();

    // Cycle 2: burn 30s
    time.advance_ms(30000);
    state.set_flame(false);
    svc.poll();

    int burns = svc.cycle_count();
    float avg = svc.avg_burn();

    INFO("burns=" << burns << " avg_burn=" << avg);

    CHECK(burns == 2);
    // 2 burns: 50s and 30s → avg = 40s
    CHECK(avg > 30.0f);
    CHECK(avg < 50.0f);
}

TEST_CASE("BurnCycleService: correct burn tracking with single cycle", "[burn_cycle]")
{
    // Happy path: one complete burn cycle
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Flame on at t=100s
    time.advance_ms(100000);
    state.set_flame(true);
    svc.poll();

    // Run two polls during burn
    time.advance_ms(10000);
    svc.poll();
    time.advance_ms(10000);
    svc.poll();

    // Flame off after 20s total burn
    state.set_flame(false);
    svc.poll();

    int cycles = svc.cycle_count();
    REQUIRE(cycles >= 1);

    // Avg burn should be > 0
    float avg = svc.avg_burn();
    CHECK(avg >= 0.0f);
}

TEST_CASE("BurnCycleService: reset zeroes all state", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Create a cycle
    state.set_flame(true);
    svc.poll();
    time.advance_ms(20000);
    state.set_flame(false);
    svc.poll();

    REQUIRE(svc.cycle_count() > 0);

    svc.reset();

    CHECK(svc.cycle_count() == 0);
    CHECK(svc.burner_hours() == 0.0f);
    CHECK(svc.avg_burn() == 0.0f);
    CHECK(svc.median_burn() == 0.0f);
}

TEST_CASE("BurnCycleService: idle when flame unchanged", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // No flame, multiple polls
    for (int i = 0; i < 5; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    CHECK(svc.cycle_count() == 0);
    CHECK(svc.burner_hours() == 0.0f);
}
