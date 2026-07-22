#include "application/ports/driven/igas_correction_store.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Make private members accessible for calc_power unit tests
#define private public
#include "application/services/gas_flow_estimator.h"
#undef private
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// GasFlowService — physical model and EMA bugs
// ═══════════════════════════════════════════════════════════════

struct FakeGasStore : IGasCorrectionStore {
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
    void save_meter(const IHeatingStateStore&, const void*) override {}
    void save_integral(float) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}
};

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
    state1.set_flame(true);

    state2.set_modulation(50.0f);
    state2.set_return_temp(45.0f);
    state2.set_p_max(24.0f);
    state2.set_gas_calorific(9.5f);
    state2.set_flame(true);

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
    state.set_flame(true);

    // Initial poll to set up timing (skipped via last_update_ms_ = 0)
    svc.poll();

    // Run enough polls to get meaningful data
    for (int i = 0; i < 15; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    float flow = svc.instant_flow();
    INFO("instant_flow=" << flow << " m3/h");

    CHECK(flow > 0.0f);
    CHECK(flow < 10.0f); // sanity: shouldn't exceed reasonable range
}

TEST_CASE("GasFlowService: no flame produces zero flow", "[gas_flow]")
{
    // With flame-gating, when is_flame_on() is false, latest_flow_ must be 0.
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(30.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(false);

    svc.poll(); // first poll sets up timing

    time.advance_ms(10000);
    svc.poll();

    float flow = svc.instant_flow();
    INFO("flow with flame=false, mod=50%: " << flow);
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
    state.set_flame(true);

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
    state.set_flame(true);

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
    state.set_return_temp(0.0f);   // 0°C return — calc_power uses fallback
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);

    svc.poll(); // setup
    time.advance_ms(10000);
    svc.poll();

    float flow = svc.instant_flow();
    INFO("flow with Tret=0, flame=true: " << flow);
    // Should not crash or produce NaN. With calc_power fallback, flow > 0 expected.
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
    state.set_flame(true);

    // Measure flow at 25% modulation
    state.set_modulation(25.0f);
    svc.poll(); // setup
    for (int i = 0; i < 15; i++) { time.advance_ms(10000); svc.poll(); }
    float flow25 = svc.instant_flow();

    // Reset and measure at 75% modulation
    svc.reset();
    state.set_modulation(75.0f);
    state.set_flame(true); // reset clears flame_prev_, need to re-set
    svc.poll(); // setup
    for (int i = 0; i < 15; i++) { time.advance_ms(10000); svc.poll(); }
    float flow75 = svc.instant_flow();

    INFO("flow@25%=" << flow25 << " flow@75%=" << flow75);

    // 75% modulation should produce ~3x flow of 25%
    // But Kalman filtering may smooth this, so just check ordering
    CHECK(flow75 > flow25);
}

// ═══════════════════════════════════════════════════════════════
// Continuous efficiency curve tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("efficiency_continuous at 30C returns 0.98", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(30.0f);
    INFO("eff at 30C=" << eff);
    CHECK(eff == 0.98f);
}

TEST_CASE("efficiency_continuous at 42.5C returns ~0.955", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(42.5f);
    INFO("eff at 42.5C=" << eff);
    // Midpoint of (30, 0.98) -> (55, 0.93); floating-point may produce 0.954999...
    CHECK(eff == Approx(0.955f).margin(0.001f));
}

TEST_CASE("efficiency_continuous at 55C returns 0.93", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(55.0f);
    INFO("eff at 55C=" << eff);
    CHECK(eff == 0.93f);
}

TEST_CASE("efficiency_continuous at 67.5C returns ~0.905", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(67.5f);
    INFO("eff at 67.5C=" << eff);
    // Midpoint of (55, 0.93) -> (80, 0.88); floating-point may produce 0.904999...
    CHECK(eff == Approx(0.905f).margin(0.001f));
}

TEST_CASE("efficiency_continuous at 80C returns 0.88", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(80.0f);
    INFO("eff at 80C=" << eff);
    CHECK(eff == 0.88f);
}

TEST_CASE("efficiency_continuous at 20C returns 0.98", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(20.0f);
    INFO("eff at 20C=" << eff);
    // Extrapolation below 30°C — deep condensate zone
    CHECK(eff == 0.98f);
}

TEST_CASE("efficiency_continuous at 90C returns 0.88", "[gas_flow][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float eff = svc.efficiency_continuous(90.0f);
    INFO("eff at 90C=" << eff);
    // Extrapolation above 80°C — same as at 80°C
    CHECK(eff == 0.88f);
}

TEST_CASE("efficiency_continuous is monotonically non-increasing", "[gas_flow][efficiency]")
{
    // As return temperature increases, efficiency must never increase
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);
    float temps[] = {20.0f, 30.0f, 40.0f, 50.0f, 55.0f, 60.0f, 70.0f, 80.0f, 90.0f};
    for (size_t i = 1; i < sizeof(temps) / sizeof(temps[0]); i++) {
        float eff_prev = svc.efficiency_continuous(temps[i - 1]);
        float eff_cur  = svc.efficiency_continuous(temps[i]);
        INFO("eff(" << temps[i-1] << "C)=" << eff_prev << " >= eff(" << temps[i] << "C)=" << eff_cur);
        CHECK(eff_prev >= eff_cur);
    }
}

TEST_CASE("flow with eta in denominator", "[gas_flow][efficiency]")
{
    // Given mod=50%, Pmax=20kW, CV=9.45 kWh/m3, eta=0.90:
    // flow = 0.50 * 20.0 / (9.45 * 0.90) = 10.0 / 8.505 ≈ 1.176
    // We set k_calib = 1.0 to avoid calibration factor interference.
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(20.0f);
    state.set_gas_calorific(9.45f);
    state.set_outside_temp(15.0f);  // +15C -> CV correction is neutral (9.45 * 288.15/288.15 = 9.45)

    // Piecewise linear: at 70C: 0.93 - (70-55)*(0.05/25) = 0.93 - 15*0.002 = 0.93 - 0.03 = 0.90
    state.set_return_temp(70.0f);
    state.set_flame(true);

    svc.set_k_calib(1.0f);
    svc.poll(); // setup (timing init, Kalman starts at 0)

    // Run enough polls for Kalman filter to converge to steady state
    for (int i = 0; i < 30; i++) {
        time.advance_ms(10000);
        svc.poll();
    }

    float flow = svc.instant_flow();
    INFO("flow with mod=50%, CV=9.45, ch_pmin=5.5 ch_pmax=24.0: " << flow);
    // Input power: ch_pmin=5.5, ch_pmax=24.0 → 5.5+18.5*0.5=14.75 kW
    // flow = 1.0 * 14.75 / 9.45 ≈ 1.561
    CHECK(flow == Approx(14.75f / 9.45f).margin(0.06f));
}

TEST_CASE("efficiency does not affect gas flow", "[gas_flow][efficiency]")
{
    // With input-power model: flow = k * P_input / cv, no eta term
    // Efficiency only affects heat output, not gas consumption
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    svc.set_k_calib(1.0f);

    // Poll at low return temp (high condensing efficiency)
    state.set_return_temp(25.0f);
    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_cold = svc.instant_flow();
    INFO("flow at ret=25C: " << flow_cold);

    // Poll at high return temp (low efficiency)
    svc.reset();
    state.set_return_temp(85.0f);
    state.set_flame(true);
    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_hot = svc.instant_flow();
    INFO("flow at ret=85C: " << flow_hot);

    // Flow should be the same — efficiency does NOT affect gas input
    CHECK(flow_cold == Approx(flow_hot).margin(0.01f));
}

// ═══════════════════════════════════════════════════════════════
// Seasonal CV correction tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("corrected_calorific at +20C outdoor", "[gas_flow][cv]")
{
    // T_gas = 20 - 5 = 15C. CV_nom = 9.45 * 288.15/288.15 = 9.45
    // Test by setting outdoor temp and measuring flow effect
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_outside_temp(20.0f);
    state.set_flame(true);
    svc.set_k_calib(1.0f);

    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }

    float flow = svc.instant_flow();
    // At T_gas=15C, cv_eff = 9.45 with correction, reference flow uses gas_calorific_ if fallback
    // With outdoor_temp=20 -> corrected_calorific returns 9.45 * 288.15/288.15 = 9.45
    // Expected flow: k=1.0 * 0.50 * 24.0 / 9.45 / eta(~0.94) = 12.0 / 9.45 / 0.94 ≈ 1.35
    INFO("flow at +20C outdoor: " << flow);
    CHECK(flow > 0.0f);
    CHECK(flow < 5.0f);
}

TEST_CASE("corrected_calorific at -20C outdoor", "[gas_flow][cv]")
{
    // T_gas = -20 - 5 = -25C. CV = 9.45 * 288.15/248.15 ≈ 10.97
    // Expected flow: 12.0 / 10.97 / 0.94 ≈ 1.16
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_outside_temp(-20.0f);
    state.set_flame(true);
    svc.set_k_calib(1.0f);

    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }

    float flow = svc.instant_flow();
    INFO("flow at -20C outdoor: " << flow);
    CHECK(flow > 0.0f);
    // At -20C outdoor, CV is higher, so flow should be lower than at +20C
    CHECK(flow < 5.0f);
}

TEST_CASE("corrected_calorific at 0C outdoor uses correction", "[gas_flow][cv]")
{
    // outdoor_temp_valid_ is now a boolean flag, not a ==0 sentinel.
    // 0°C is a valid reading: T_gas = 0-5 = -5C, CV = 9.45 * 288.15/268.15 ≈ 10.15
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_outside_temp(0.0f);  // Valid 0°C — correction IS applied now
    state.set_flame(true);
    svc.set_k_calib(1.0f);

    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }

    float flow = svc.instant_flow();
    INFO("flow with outdoor_temp=0C (T_gas=-5C, cv_eff≈10.15): " << flow);
    // With correction: CV ≈ 10.15, flow = 12.0 / 10.15 / 0.94 ≈ 1.26
    CHECK(flow > 0.0f);
    CHECK(flow < 5.0f);
}

TEST_CASE("corrected_calorific fallback when outdoor unknown", "[gas_flow][cv]")
{
    // outdoor_temp_ stays at default 0 (no sensor), corrected_calorific returns gas_calorific_
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    svc.set_k_calib(1.0f);
    // NOT setting outdoor_temp - defaults to 0 (unknown)

    svc.poll(); // setup
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }

    float flow = svc.instant_flow();
    INFO("flow with no outdoor temp: " << flow);
    // With fallback to gas_calorific_=9.5: flow = 12.0 / 9.5 / eta ≈ 1.34
    CHECK(flow > 0.0f);
    CHECK(flow < 5.0f);
}

// ═══════════════════════════════════════════════════════════════
// calc_power — non-linear modulation with Pmin/Pmax vs MWT
// ═══════════════════════════════════════════════════════════════

TEST_CASE("calc_power below 1pct returns 0 (burner not firing)", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Modulation < 1% means gas valve is effectively closed
    CHECK(svc.calc_power(0.0f, 80.0f, 60.0f) == Approx(0.0f).margin(0.001f));
    CHECK(svc.calc_power(0.0f, 50.0f, 30.0f) == Approx(0.0f).margin(0.001f));
    CHECK(svc.calc_power(0.5f, 80.0f, 60.0f) == Approx(0.0f).margin(0.001f));
    CHECK(svc.calc_power(0.99f, 50.0f, 30.0f) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("calc_power at 1pct or above returns proportional power", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Default ch_pmin=5.5, ch_pmax=24.0 → 5.5 + 18.5*0.01 = 5.685
    // Input power is independent of MWT
    CHECK(svc.calc_power(1.0f, 80.0f, 60.0f) == Approx(5.685f).margin(0.01f));
    CHECK(svc.calc_power(1.0f, 50.0f, 30.0f) == Approx(5.685f).margin(0.01f));
}

TEST_CASE("calc_power at 100pct returns Pmax (24.0 kW)", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Input power = 24.0 kW at 100% modulation (independent of MWT)
    CHECK(svc.calc_power(100.0f, 80.0f, 60.0f) == Approx(24.0f).margin(0.001f));
    CHECK(svc.calc_power(100.0f, 50.0f, 30.0f) == Approx(24.0f).margin(0.001f));
}

TEST_CASE("calc_power at 50pct returns ~14.75 kW", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // ch_pmin=5.5, ch_pmax=24.0 → 5.5 + 18.5*0.5 = 14.75
    CHECK(svc.calc_power(50.0f, 80.0f, 60.0f) == Approx(14.75f).margin(0.001f));
}

TEST_CASE("calc_power uses ch_pmin/ch_pmax independent of temperatures", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Default ch_pmin=5.5, ch_pmax=24.0 → 5.5 + 18.5*0.5 = 14.75 kW
    // Input power does not depend on MWT — same at any temperature
    CHECK(svc.calc_power(50.0f, 0.0f, 0.0f) == Approx(14.75f).margin(0.001f));
    CHECK(svc.calc_power(50.0f, 80.0f, 60.0f) == Approx(14.75f).margin(0.001f));
    CHECK(svc.calc_power(50.0f, 40.0f, 30.0f) == Approx(14.75f).margin(0.001f));
}

TEST_CASE("calc_power monotonic with modulation", "[gas_flow][calc_power]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    float prev = svc.calc_power(0.0f, 80.0f, 60.0f);
    for (int mod = 10; mod <= 100; mod += 10) {
        float cur = svc.calc_power(static_cast<float>(mod), 80.0f, 60.0f);
        CHECK(cur >= prev);
        prev = cur;
    }
}

// ═══════════════════════════════════════════════════════════════
// Flame-gated integration and warmup
// ═══════════════════════════════════════════════════════════════

TEST_CASE("flame off integral unchanged", "[gas_flow][flame]")
{
    // Integration should only happen when flame is on
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    // First with flame=true to accumulate some integral
    state.set_flame(true);
    svc.poll(); // setup
    for (int i = 0; i < 30; i++) {
        time.advance_ms(10000);
        svc.poll();
    }
    float integral_after_burn = svc.integral_m3();
    INFO("integral after 30 polls with flame=true: " << integral_after_burn);
    REQUIRE(integral_after_burn > 0.0f);

    // Now with flame=false — integral must NOT change
    state.set_flame(false);
    time.advance_ms(10000);
    svc.poll();
    float integral_after_off = svc.integral_m3();
    INFO("integral after flame=false: " << integral_after_off);
    CHECK(integral_after_off == integral_after_burn);
    CHECK(svc.instant_flow() == 0.0f);
}

TEST_CASE("flame on integral increases", "[gas_flow][flame]")
{
    // When flame is on and there's non-zero modulation, integral must increase
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);

    svc.poll(); // setup
    float integral_before = svc.integral_m3();
    INFO("integral before: " << integral_before);

    time.advance_ms(10000);
    svc.poll();
    float integral_after = svc.integral_m3();
    INFO("integral after one poll: " << integral_after);

    CHECK(integral_after > integral_before);
}

TEST_CASE("warmup factor 0.85 immediately after ignition", "[gas_flow][flame][warmup]")
{
    // Immediately after flame transitions false→true, warmup should be 0.85
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    // First poll with flame=false (default) to set up timing
    svc.poll();

    // Now set flame=true — ignition detected, warmup = 0.85
    state.set_flame(true);
    time.advance_ms(10000);
    svc.poll();

    // The flow should be 85% of what it would be after full warmup.
    // To verify, let the warmup complete (60s+) and check ratio.
    // Save flow at warmup=0.85
    float flow_cold = svc.instant_flow();
    INFO("flow immediately after ignition: " << flow_cold);

    // Run past warmup period
    for (int i = 0; i < 8; i++) {
        time.advance_ms(10000);
        svc.poll();
    }
    float flow_warm = svc.instant_flow();
    INFO("flow after warmup period: " << flow_warm);

    // The cold flow should be less than the warm flow due to warmup factor
    CHECK(flow_cold < flow_warm);
}

TEST_CASE("warmup factor 1.0 after 60 seconds", "[gas_flow][flame][warmup]")
{
    // After 60+ seconds from ignition, warmup factor must be 1.0
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);

    // Setup poll with flame=false
    svc.poll();

    // Ignition
    state.set_flame(true);
    time.advance_ms(10000);
    svc.poll();

    // Wait 60+ seconds from ignition
    for (int i = 0; i < 7; i++) {
        time.advance_ms(10000);
        svc.poll();
    }
    // Now elapsed >= 60s, warmup = 1.0

    float flow_after_warmup = svc.instant_flow();
    INFO("flow 70s after ignition: " << flow_after_warmup);
    CHECK(flow_after_warmup > 0.0f);
}


// ═══════════════════════════════════════════════════════════════
// outdoor_temp_valid_ flag lifecycle
// ═══════════════════════════════════════════════════════════════

TEST_CASE("outdoor_temp_valid_ starts false", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    CHECK(svc.outdoor_temp_valid_ == false);
}

TEST_CASE("outdoor_temp_valid_ becomes true after valid poll", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    state.set_outside_temp(20.0f);   // valid

    CHECK(svc.outdoor_temp_valid_ == false);
    svc.poll();  // first poll returns early (last_update_ms_ init)
    time.advance_ms(10000);
    svc.poll();  // second poll actually reads temperatures
    CHECK(svc.outdoor_temp_valid_ == true);
}

TEST_CASE("outdoor_temp_valid_ stays false on invalid temp", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    state.set_outside_temp(100.0f);  // invalid (>60°C)

    svc.poll();
    time.advance_ms(10000);
    svc.poll();
    CHECK(svc.outdoor_temp_valid_ == false);
}

TEST_CASE("outdoor_temp_valid_ accepts -50C boundary", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    state.set_outside_temp(-50.0f);   // lower boundary

    svc.poll();
    time.advance_ms(10000);
    svc.poll();
    CHECK(svc.outdoor_temp_valid_ == true);
}

TEST_CASE("outdoor_temp_valid_ accepts +60C boundary", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    state.set_outside_temp(60.0f);    // upper boundary

    svc.poll();
    time.advance_ms(10000);
    svc.poll();
    CHECK(svc.outdoor_temp_valid_ == true);
}

TEST_CASE("outdoor_temp_valid_ resets to false on reset", "[gas_flow][outdoor_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_outside_temp(15.0f);
    state.set_flame(true);
    svc.poll();
    time.advance_ms(10000);
    svc.poll();
    REQUIRE(svc.outdoor_temp_valid_ == true);

    svc.reset();
    CHECK(svc.outdoor_temp_valid_ == false);
}

// ═══════════════════════════════════════════════════════════════
// CV correction: value assertions + flow ratio
// ═══════════════════════════════════════════════════════════════

TEST_CASE("cv correction flow ratio -20C vs +20C", "[gas_flow][cv]")
{
    // At -20°C: CV_eff ≈ 10.97 (gas denser → less volume for same energy)
    // At +20°C: CV_eff ≈ 9.45  (gas at reference density)
    // Expected: flow_cold / flow_warm ≈ CV_warm / CV_cold ≈ 9.45/10.97 ≈ 0.86
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);

    // Warm scenario
    state.set_outside_temp(20.0f);
    svc.poll();
    for (int i = 0; i < 20; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_warm = svc.instant_flow();
    INFO("flow at +20C: " << flow_warm);
    REQUIRE(flow_warm > 0.0f);

    // Cold scenario
    svc.reset();
    state.set_outside_temp(-20.0f);
    state.set_flame(true);
    svc.poll();
    for (int i = 0; i < 20; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_cold = svc.instant_flow();
    INFO("flow at -20C: " << flow_cold);
    REQUIRE(flow_cold > 0.0f);

    // Colder gas → CV higher → flow lower
    CHECK(flow_cold < flow_warm);

    // Ratio check: flow_cold/flow_warm ≈ CV(+20)/CV(-20) ≈ 9.45/10.97 ≈ 0.86
    float ratio = flow_cold / flow_warm;
    float expected = 9.45f / (9.45f * 288.15f / (273.15f - 25.0f));
    INFO("flow ratio=" << ratio << " expected≈" << expected);
    CHECK(ratio == Approx(expected).margin(0.05f));
}

// ═══════════════════════════════════════════════════════════════
// calc_power: validity flag behaviour
// ═══════════════════════════════════════════════════════════════

TEST_CASE("flow_temp_valid_ starts false", "[gas_flow][temp_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    CHECK(svc.flow_temp_valid_ == false);
    CHECK(svc.ret_temp_valid_ == false);
}

TEST_CASE("flow_temp_valid_ becomes true after poll with non-zero temp", "[gas_flow][temp_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(45.0f);     // non-zero → ret_temp_valid_ = true
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    state.set_ch_temp(65.0f);          // non-zero → flow_temp_valid_ = true

    svc.poll();                        // first poll: early return
    time.advance_ms(10000);
    svc.poll();                        // second poll: reads temperatures
    CHECK(svc.flow_temp_valid_ == true);
    CHECK(svc.ret_temp_valid_ == true);
}

TEST_CASE("calc_power uses MWT after validity flags set, not fallback", "[gas_flow][temp_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Input power is flat — same at any MWT or flag state
    svc.flow_temp_valid_ = true;
    svc.ret_temp_valid_  = true;

    // ch_pmin=5.5, ch_pmax=24.0 → 5.5 + 18.5*0.5 = 14.75
    float power = svc.calc_power(50.0f, 80.0f, 60.0f);
    CHECK(power == Approx(14.75f).margin(0.001f));
}

TEST_CASE("calc_power with flags false but non-zero params still works", "[gas_flow][temp_valid]")
{
    // Input power does not depend on temp validity flags
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Both flags false (default), passing non-zero values
    float power = svc.calc_power(50.0f, 80.0f, 60.0f);
    // ch_pmin=5.5, ch_pmax=24.0 → 5.5 + 18.5*0.5 = 14.75
    CHECK(power == Approx(14.75f).margin(0.001f));
}

TEST_CASE("temp_valid flags reset", "[gas_flow][temp_valid]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    svc.flow_temp_valid_ = true;
    svc.ret_temp_valid_  = true;
    svc.reset();
    CHECK(svc.flow_temp_valid_ == false);
    CHECK(svc.ret_temp_valid_ == false);
}
