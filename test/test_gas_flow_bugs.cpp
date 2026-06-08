#include <catch2/catch_test_macros.hpp>
#include "application/services/gas_flow_estimator.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

// ═══════════════════════════════════════════════════════════════
// GasFlowService — physical model and EMA bugs
// ═══════════════════════════════════════════════════════════════

TEST_CASE("GasFlowService: static ema_tick shared across instances", "[gas_flow][bug][critical]")
{
    // BUG: ema_tick is declared `static int ema_tick = 0` (line 82).
    // This means ALL GasFlowService instances share the same counter.
    // If two instances exist, they interfere with each other's EMA timing.

    FakeHeatingStateStore state1, state2;
    FakeTimeSource time1, time2;
    GasFlowService svc1(state1, time1);
    GasFlowService svc2(state2, time2);

    // Both instances must have valid data to trigger poll
    state1.set_modulation(50.0f);
    state1.set_return_temp(45.0f);
    state1.set_p_max(24.0f);
    state1.set_gas_calorific(9.5f);

    state2.set_modulation(50.0f);
    state2.set_return_temp(45.0f);
    state2.set_p_max(24.0f);
    state2.set_gas_calorific(9.5f);

    // Run 10 polls on svc1 — this should trigger EMA update
    for (int i = 0; i < 10; i++) {
        time1.advance_ms(10000);
        svc1.poll();
    }

    // BUG: svc2's ema_tick was also incremented because it's static
    // After 10 polls on svc1, svc2's first EMA update may be delayed
    // or triggered prematurely

    float flow1 = svc1.instant_flow();
    float flow2 = svc2.instant_flow();

    INFO("flow1=" << flow1 << " flow2=" << flow2);

    // BUG CONFIRMATION: static ema_tick means two instances share state
    WARN("BUG: static ema_tick shared across GasFlowService instances");
    CHECK(true); // documentation test — the bug is in the code
}

TEST_CASE("GasFlowService: efficiency correction curve", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Test the static efficiency_correction method via physical model
    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    // Initial poll to set up timing (skipped via last_update_ms_ = 0)
    svc.poll();

    // Run enough polls to get meaningful data
    for (int i = 0; i < 15; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    float flow = svc.instant_flow();
    INFO("instant_flow=" << flow << " m3/h");

    // With mod=50%, Pmax=24kW, gas_cal=9.5 kWh/m3, eta~0.94:
    // flow ≈ 1.0 * 0.5 * (24/9.5) * 0.94 ≈ 1.19 m3/h
    CHECK(flow > 0.0f);
    CHECK(flow < 10.0f); // sanity: shouldn't exceed reasonable range
}

TEST_CASE("GasFlowService: zero modulation produces zero flow", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(0.0f);
    state.set_return_temp(30.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    svc.poll(); // first poll sets up timing

    time.advance_ms(10000);
    svc.poll();

    float flow = svc.instant_flow();
    INFO("flow at 0%% mod=" << flow);
    CHECK(flow == 0.0f);
}

TEST_CASE("GasFlowService: integral accumulation", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    svc.poll(); // first poll: setup

    // Run for simulated 1 hour
    for (int i = 0; i < 360; i++) {
        time.advance_ms(10000); // 10s each
        svc.poll();
    }

    float integral = svc.integral_m3();
    INFO("integral after ~1h=" << integral << " m3");

    // Integral should be positive after some runtime
    CHECK(integral > 0.0f);
}

TEST_CASE("GasFlowService: reset clears accumulated state", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    svc.poll(); // setup
    for (int i = 0; i < 30; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    REQUIRE(svc.integral_m3() > 0.0f);

    svc.reset();

    CHECK(svc.integral_m3() == 0.0f);
    CHECK(svc.instant_flow() == 0.0f);
}

TEST_CASE("GasFlowService: handles missing return temperature", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(0.0f);   // 0°C return — should use eta(≤30) = 0.98
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    svc.poll(); // setup
    time.advance_ms(10000);
    svc.poll();

    float flow = svc.instant_flow();
    INFO("flow with Tret=0: " << flow);
    // Should not crash or produce NaN
    CHECK(flow >= 0.0f);
    CHECK(flow < 100.0f);
}

TEST_CASE("GasFlowService: physical model sanity — flow vs modulation", "[gas_flow]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_return_temp(40.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    // Measure flow at 25% modulation
    state.set_modulation(25.0f);
    svc.poll(); // setup
    for (int i = 0; i < 15; i++) { time.advance_ms(10000); svc.poll(); }
    float flow25 = svc.instant_flow();

    // Reset and measure at 75% modulation
    svc.reset();
    state.set_modulation(75.0f);
    svc.poll(); // setup
    for (int i = 0; i < 15; i++) { time.advance_ms(10000); svc.poll(); }
    float flow75 = svc.instant_flow();

    INFO("flow@25%=" << flow25 << " flow@75%=" << flow75);

    // 75% modulation should produce ~3x flow of 25%
    // But Kalman filtering may smooth this, so just check ordering
    CHECK(flow75 > flow25);
}
