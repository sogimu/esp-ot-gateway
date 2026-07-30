#include "application/ports/driven/iburn_stats_store.h"
/// Tests for BurnCycleService: flame edge detection, cycle tracking, averages.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/burn_cycle_service.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

struct FakeBurnStatsStore : IBurnStatsStore {
    bool load_burn_stats(uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&) override { return false; }
    void save_burn_stats(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
};

TEST_CASE("BurnCycle: initial state is empty", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    REQUIRE(bcs.cycle_count() == 0);
    REQUIRE(bcs.burner_seconds() == 0);
    REQUIRE(bcs.avg_burn_sec() == Approx(0.0f));
    REQUIRE(bcs.avg_pause_sec() == Approx(0.0f));
    REQUIRE(bcs.burner_hours() == Approx(0.0f));
}

TEST_CASE("BurnCycle: detects flame on→off edge", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    state.set_flame(false);
    bcs.execute();

    time.advance_sec(10);

    state.set_flame(true);
    bcs.execute();

    time.advance_sec(60);

    state.set_flame(false);
    bcs.execute();

    REQUIRE(bcs.cycle_count() == 1);
    REQUIRE(bcs.burner_seconds() >= 55);
}

TEST_CASE("BurnCycle: accumulates burner seconds on flame-off edge", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    state.set_flame(true);
    bcs.execute();

    for (int i = 0; i < 5; i++) {
        time.advance_ms(1100);
        bcs.execute();
    }

    state.set_flame(false);
    bcs.execute();

    REQUIRE(bcs.burner_seconds() >= 3);
}

TEST_CASE("BurnCycle: no cycles counted when flame stays off", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    state.set_flame(false);

    for (int i = 0; i < 10; i++) {
        time.advance_ms(1100);
        bcs.execute();
    }

    REQUIRE(bcs.cycle_count() == 0);
}

TEST_CASE("BurnCycle: multiple complete burn cycles", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    for (int c = 0; c < 3; c++) {
        state.set_flame(true);
        bcs.execute();
        time.advance_sec(30);
        bcs.execute();

        state.set_flame(false);
        bcs.execute();
        time.advance_sec(10);
        bcs.execute();
    }

    REQUIRE(bcs.cycle_count() == 3);
}

TEST_CASE("BurnCycle: average burn computed from cumulative counters", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    // Two cycles: 30s and 60s
    state.set_flame(true);
    bcs.execute();
    time.advance_sec(30);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();
    time.advance_sec(10);
    bcs.execute();

    state.set_flame(true);
    bcs.execute();
    time.advance_sec(60);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();

    REQUIRE(bcs.cycle_count() == 2);
    // avg = (30+60)/2 = 45
    float avg = bcs.avg_burn_sec();
    REQUIRE(avg >= 40.0f);
    REQUIRE(avg <= 50.0f);
}

TEST_CASE("BurnCycle: burner_hours converts seconds to hours", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    state.set_flame(true);
    bcs.execute();
    time.advance_sec(3600);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();

    float hours = bcs.burner_hours();
    REQUIRE(hours >= 0.5f);
}

TEST_CASE("BurnCycle: pause classification by 10min threshold", "[burn][app]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService bcs(state, time, burn_store);

    // First burn
    state.set_flame(true);
    bcs.execute();
    time.advance_sec(30);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();

    // Short pause (3 min) — modulation
    time.advance_sec(180);
    bcs.execute();
    state.set_flame(true);
    bcs.execute();
    time.advance_sec(30);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();

    // Long pause (15 min) — inter-session
    time.advance_sec(900);
    bcs.execute();
    state.set_flame(true);
    bcs.execute();
    time.advance_sec(30);
    bcs.execute();
    state.set_flame(false);
    bcs.execute();

    REQUIRE(bcs.modulation_cnt() == 1);
    REQUIRE(bcs.inter_session_cnt() == 1);
    REQUIRE(bcs.modulation_pause_sec() >= 170);
    REQUIRE(bcs.inter_session_pause_sec() >= 890);
}
