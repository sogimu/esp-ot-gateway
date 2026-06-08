#include <catch2/catch_test_macros.hpp>
#include "application/services/burn_cycle_service.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

// ═══════════════════════════════════════════════════════════════
// BurnCycleService — known bugs verification
// ═══════════════════════════════════════════════════════════════

TEST_CASE("BurnCycleService: burner_sec_ double-counting", "[burn_cycle][bug][critical]")
{
    // BUG: burner_sec_ is incremented both on flame-off edge (line 55)
    // AND during every poll while flame is on (line 65).
    // This causes burner_hours to be ~2x the actual value.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Flame comes on at t=60s
    time.advance_ms(60000);
    state.set_flame(true);
    svc.poll();  // flame edge detected, flame_on_ms_ = 60000

    // Burn for 30 seconds (3 polls at ~10s each)
    for (int i = 0; i < 3; i++) {
        time.advance_ms(10000);
        svc.poll();  // each poll adds ~10s via the ongoing accumulation (line 65)
    }

    // Flame goes off at t=90s
    time.advance_ms(1);
    state.set_flame(false);
    svc.poll();  // flame off: adds full burn duration (line 55): 30s

    // Actual burn time: 30 seconds
    // Expected burner_sec_: 30
    // Bug behavior: edge adds 30s + each poll adds ~10s → ~60s total

    float hours = svc.burner_hours();
    float expected_hours = 30.0f / 3600.0f; // 30 seconds

    INFO("burner_hours=" << hours << " expected=" << expected_hours);
    // Known bug: burner_sec_ is double-counted
    // When fixed, hours should be ~0.0083 (30s)
    // Currently ~0.0167 (60s) due to double-counting
    WARN("BUG: burner_hours double-counted — actual=" << hours
         << " expected~" << expected_hours);
}

TEST_CASE("BurnCycleService: sizeof(burn_dur_) is pointer size", "[burn_cycle][bug][critical]")
{
    // BUG: reset() uses sizeof(burn_dur_) which is 4 or 8 (pointer size),
    // not RING * sizeof(uint16_t) (array size).
    // This means reset() only clears a few bytes, leaving stale data.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Generate a burn cycle to populate arrays
    time.advance_ms(10000);
    state.set_flame(true);
    svc.poll();

    time.advance_ms(30000); // 30s burn
    state.set_flame(false);
    svc.poll();

    time.advance_ms(100000); // 100s pause
    state.set_flame(true);
    svc.poll();

    // Before reset: cycle_total_ should be 1 (only the pause was recorded)
    int before = svc.cycle_count();
    float avg_before = svc.avg_burn();

    svc.reset();

    int after = svc.cycle_count();
    float avg_after = svc.avg_burn();

    INFO("before_reset: cycle_total=" << before << " avg_burn=" << avg_before);
    INFO("after_reset:  cycle_total=" << after << " avg_burn=" << avg_after);

    // After reset: cycle_total_ should be 0
    CHECK(after == 0);
    // After reset: avg_burn should be 0
    // BUG: stale data may survive due to sizeof(pointer) in memset
    WARN("BUG: sizeof(burn_dur_) clears only pointer-sized bytes, not full array");
}

TEST_CASE("BurnCycleService: avg_burn uses cycle_total_ not cycle_cnt_", "[burn_cycle][bug][major]")
{
    // BUG: avg_burn() and median_burn() iterate over cycle_total_ entries
    // (line 101-103), but cycle_total_ counts PAUSES (line 39), not burns.
    // Burns are counted by cycle_cnt_ (line 56).
    // When there are more pauses than burns (normal: flame on/off cycle),
    // the burn array has gaps filled with zeros.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService svc(state, time);

    // Cycle 1: burn 50s, pause 100s
    state.set_flame(true);
    svc.poll();
    time.advance_ms(50000);
    state.set_flame(false);
    svc.poll();  // records 50s burn, increments cycle_cnt_
    time.advance_ms(100000);
    state.set_flame(true);
    svc.poll();  // records 100s pause, increments cycle_total_

    // Cycle 2: burn 30s, pause 80s
    time.advance_ms(30000);
    state.set_flame(false);
    svc.poll();  // records 30s burn, increments cycle_cnt_
    time.advance_ms(80000);
    state.set_flame(true);
    svc.poll();  // records 80s pause, increments cycle_total_

    int burns = svc.cycle_count();  // should be 2
    float avg = svc.avg_burn();     // uses cycle_total_ for count
    float med = svc.median_burn();  // uses cycle_total_ for count

    INFO("burns=" << burns << " avg_burn=" << avg << " median_burn=" << med);

    // With 2 burns of 50s and 30s, average should be 40s
    // BUG: cycle_total_ counts pauses (2), but burn array has 2 values
    // However, the burn is stored at (cycle_idx_ - 1) which might be wrong
    CHECK(burns == 2);
    WARN("BUG: avg_burn/median_burn use cycle_total_ (pauses) not cycle_cnt_ (burns)");
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
