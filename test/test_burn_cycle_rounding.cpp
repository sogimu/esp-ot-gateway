/// Tests for BurnCycleService burn/pause duration rounding.
/// Regression: integer division truncated (lost up to 0.999s per cycle).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/burn_cycle_service.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

TEST_CASE("BurnCycle: burn duration 1.5s rounds to 2s", "[burn]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService bcs(state, time);

    state.set_flame(true);
    bcs.poll();
    time.advance_ms(1500);
    bcs.poll();
    state.set_flame(false);
    bcs.poll();

    REQUIRE(bcs.burner_seconds() == 2);
    REQUIRE(bcs.cycle_count() == 1);
}

TEST_CASE("BurnCycle: burn duration 0.4s rounds to 0s", "[burn]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService bcs(state, time);

    state.set_flame(true);
    bcs.poll();
    time.advance_ms(400);
    bcs.poll();
    state.set_flame(false);
    bcs.poll();

    REQUIRE(bcs.cycle_count() == 1);
}

TEST_CASE("BurnCycle: three 0.6s burns accumulate to 3s with rounding", "[burn]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService bcs(state, time);

    for (int c = 0; c < 3; c++) {
        state.set_flame(true);
        bcs.poll();
        time.advance_ms(600);
        bcs.poll();
        state.set_flame(false);
        bcs.poll();
        time.advance_ms(200);
        bcs.poll();
    }

    REQUIRE(bcs.burner_seconds() == 3);
    REQUIRE(bcs.cycle_count() == 3);
}

TEST_CASE("BurnCycle: 3600s burn equals exactly 1.0 burner hours", "[burn]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService bcs(state, time);

    state.set_flame(true);
    bcs.poll();
    time.advance_sec(3600);
    bcs.poll();
    state.set_flame(false);
    bcs.poll();

    REQUIRE(bcs.burner_seconds() == 3600);
    REQUIRE(bcs.burner_hours() == Approx(1.0f));
}

TEST_CASE("BurnCycle: first cycle burner_sec is not double-counted", "[burn]") {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    BurnCycleService bcs(state, time);

    state.set_flame(true);
    bcs.poll();
    time.advance_sec(120);
    bcs.poll();
    state.set_flame(false);
    bcs.poll();

    REQUIRE(bcs.burner_seconds() >= 115);
    REQUIRE(bcs.burner_seconds() <= 125);
    REQUIRE(bcs.cycle_count() == 1);
}
