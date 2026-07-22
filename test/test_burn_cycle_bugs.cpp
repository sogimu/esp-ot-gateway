#include "application/ports/driven/iburn_stats_store.h"
/// Edge-case tests for BurnCycleService.

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

TEST_CASE("BurnCycleService: cycle count remains 0 with no transitions", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    for (int i = 0; i < 10; i++) {
        time.advance_ms(1100);
        svc.poll();
    }

    REQUIRE(svc.cycle_count() == 0);
}

TEST_CASE("BurnCycleService: reset clears all counters", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    state.set_flame(true);
    svc.poll();
    time.advance_sec(60);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    REQUIRE(svc.cycle_count() == 1);

    svc.reset();

    REQUIRE(svc.cycle_count() == 0);
    CHECK(svc.burner_seconds() == 0);
    CHECK(svc.total_pause_seconds() == 0);
    CHECK(svc.inter_session_cnt() == 0);
    CHECK(svc.modulation_cnt() == 0);
}

TEST_CASE("BurnCycleService: avg_burn_sec with one cycle", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    state.set_flame(true);
    svc.poll();
    time.advance_sec(30);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    float avg = svc.avg_burn_sec();
    REQUIRE(avg == Approx(30.0f).margin(1.0f));
}

TEST_CASE("BurnCycleService: avg_burn_sec is zero with no cycles", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    CHECK(svc.burner_hours() == 0.0f);
    CHECK(svc.avg_burn_sec() == 0.0f);
    CHECK(svc.avg_pause_sec() == 0.0f);
    CHECK(svc.avg_inter_session_pause_sec() == 0.0f);
    CHECK(svc.avg_modulation_pause_sec() == 0.0f);
}

TEST_CASE("BurnCycleService: reset zeros out everything", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    state.set_flame(true);
    svc.poll();
    time.advance_sec(60);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    svc.reset();

    CHECK(svc.burner_seconds() == 0);
    CHECK(svc.cycle_count() == 0);
    CHECK(svc.total_pause_seconds() == 0);
    CHECK(svc.burner_hours() == 0.0f);
    CHECK(svc.avg_burn_sec() == 0.0f);
}

TEST_CASE("BurnCycleService: modulation vs inter-session pause classification", "[burn_cycle]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeBurnStatsStore burn_store;
    BurnCycleService svc(state, time, burn_store);

    // Burn 1
    state.set_flame(true);
    svc.poll();
    time.advance_sec(60);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    // Short pause (3 min) → modulation
    time.advance_sec(180);
    svc.poll();
    state.set_flame(true);
    svc.poll();
    time.advance_sec(60);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    // Long pause (20 min) → inter-session
    time.advance_sec(1200);
    svc.poll();
    state.set_flame(true);
    svc.poll();
    time.advance_sec(60);
    svc.poll();
    state.set_flame(false);
    svc.poll();

    REQUIRE(svc.modulation_cnt() == 1);
    REQUIRE(svc.inter_session_cnt() == 1);
    REQUIRE(svc.modulation_pause_sec() >= 170);
    REQUIRE(svc.inter_session_pause_sec() >= 1190);
}
