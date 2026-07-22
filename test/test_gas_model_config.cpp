#include "application/ports/driven/igas_correction_store.h"
/// Tests for GasFlowService with configurable boiler model parameters:
///   - DHW branch in calc_power and efficiency
///   - CH power parameters from state (replaces hardcoded PMIN_WARM, etc.)
///   - Efficiency curve points from state
///   - Gas temperature offset from state

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#define private public
#include "application/services/gas_flow_estimator.h"
#undef private
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// DHW: calc_power
// ═══════════════════════════════════════════════════════════════

struct FakeGasStore : IGasCorrectionStore {
    FakeConfigurationStore* cfg = nullptr;
    int boiler_config_saved_ = 0;

    bool load_meter(IHeatingStateStore& s, void* blob) override {
        return cfg ? cfg->load_meter(s, blob) : false;
    }
    void save_meter(const IHeatingStateStore& s, const void* blob) override {
        if (cfg) cfg->save_meter(s, blob);
    }
    void save_integral(float v) override { if (cfg) cfg->save_integral(v); }
    void save_boiler_config(const IHeatingStateStore&) override { boiler_config_saved_++; }
};

TEST_CASE("calc_power uses dhw params when dhw_active", "[gas_model][dhw]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_dhw_pmin(6.0f);
    state.set_dhw_pmax(30.0f);
    svc.dhw_active_ = true;

    // mod=100% should give pmax (30 kW)
    float power = svc.calc_power(100.0f, 80.0f, 60.0f);
    CHECK(power == Approx(30.0f).margin(0.001f));

    // mod=0% → 0 kW (burner not firing)
    power = svc.calc_power(0.0f, 80.0f, 60.0f);
    CHECK(power == Approx(0.0f).margin(0.001f));

    // mod=50% should give midpoint
    power = svc.calc_power(50.0f, 80.0f, 60.0f);
    CHECK(power == Approx(18.0f).margin(0.001f));
}

TEST_CASE("calc_power ignores MWT in DHW mode", "[gas_model][dhw]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_dhw_pmin(7.0f);
    state.set_dhw_pmax(25.0f);
    svc.dhw_active_ = true;

    // Same modulation should give same result regardless of flow/ret temps
    float p1 = svc.calc_power(60.0f, 50.0f, 30.0f);  // MWT=40
    float p2 = svc.calc_power(60.0f, 80.0f, 60.0f);  // MWT=70

    CHECK(p1 == p2);
    CHECK(p1 == Approx(7.0f + (25.0f - 7.0f) * 0.6f).margin(0.001f));
}

// ═══════════════════════════════════════════════════════════════
// DHW: efficiency
// ═══════════════════════════════════════════════════════════════

TEST_CASE("DHW and CH have same flow with same power params", "[gas_model][dhw]")
{
    // With input-power model, flow depends only on input power and cv,
    // not on efficiency. CH and DHW with same modulation and params → same flow.
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_modulation(50.0f);
    state.set_return_temp(40.0f);
    state.set_p_max(24.0f);
    state.set_gas_calorific(9.5f);
    state.set_flame(true);
    svc.set_k_calib(1.0f);

    // First poll: CH mode — run many iterations for Kalman convergence
    svc.poll();
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_ch = svc.instant_flow();
    INFO("flow in CH mode: " << flow_ch);

    // Now switch to DHW mode — reset to clear state, then poll with dhw active
    svc.reset();
    state.set_dhw_active(true);
    state.set_flame(true);
    svc.poll();
    for (int i = 0; i < 30; i++) { time.advance_ms(10000); svc.poll(); }
    float flow_dhw = svc.instant_flow();
    INFO("flow in DHW mode: " << flow_dhw);

    CHECK(flow_ch > 0.0f);
    CHECK(flow_dhw > 0.0f);
    // With same params, CH and DHW should have equal flow (no efficiency term)
    CHECK(flow_ch == Approx(flow_dhw).margin(0.01f));
}

// ═══════════════════════════════════════════════════════════════
// CH: calc_power with state parameters
// ═══════════════════════════════════════════════════════════════

TEST_CASE("calc_power uses ch_pmin from state", "[gas_model][ch]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set a non-default ch_pmin
    state.set_ch_pmin(5.0f);

    // mod=1% → ch_pmin (5.0) + (pmax-pmin)*0.01
    // default ch_pmax=24.0 → 5.0 + 19.0*0.01 = 5.19
    float power = svc.calc_power(1.0f, 50.0f, 30.0f);
    CHECK(power == Approx(5.19f).margin(0.01f));
}

TEST_CASE("calc_power uses ch_pmax from state", "[gas_model][ch]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set a non-default ch_pmax
    state.set_ch_pmax(25.0f);
    state.set_ch_pmin(3.0f);

    // mod=100% → should use ch_pmax (25.0) — independent of MWT
    float power = svc.calc_power(100.0f, 80.0f, 60.0f);
    CHECK(power == Approx(25.0f).margin(0.001f));

    // Same at any MWT (input power doesn't depend on temperatures)
    float power2 = svc.calc_power(100.0f, 40.0f, 30.0f);
    CHECK(power2 == Approx(25.0f).margin(0.001f));
}

TEST_CASE("calc_power with non-default ch_pmin/ch_pmax", "[gas_model][ch]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    state.set_ch_pmin(4.0f);
    state.set_ch_pmax(18.0f);

    // mod=50% → 4.0 + (18.0-4.0)*0.5 = 11.0 kW
    float power = svc.calc_power(50.0f, 0.0f, 0.0f);
    CHECK(power == Approx(11.0f).margin(0.001f));
}

// ═══════════════════════════════════════════════════════════════
// efficiency_continuous with configured points
// ═══════════════════════════════════════════════════════════════

TEST_CASE("efficiency uses configured eff points", "[gas_model][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set custom efficiency points: (20, 0.99), (50, 0.90), (90, 0.85)
    state.set_eff_t1(20.0f); state.set_eff_v1(0.99f);
    state.set_eff_t2(50.0f); state.set_eff_v2(0.90f);
    state.set_eff_t3(90.0f); state.set_eff_v3(0.85f);

    // At t1 exactly
    CHECK(svc.efficiency_continuous(20.0f) == Approx(0.99f));

    // At t2 exactly
    CHECK(svc.efficiency_continuous(50.0f) == Approx(0.90f));

    // At t3 exactly
    CHECK(svc.efficiency_continuous(90.0f) == Approx(0.85f));

    // Interpolation: halfway between 20 and 50 → (0.99+0.90)/2 = 0.945
    CHECK(svc.efficiency_continuous(35.0f) == Approx(0.945f).margin(0.001f));

    // Interpolation: halfway between 50 and 90 → (0.90+0.85)/2 = 0.875
    CHECK(svc.efficiency_continuous(70.0f) == Approx(0.875f).margin(0.001f));
}

TEST_CASE("efficiency clamps below t1", "[gas_model][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set custom points
    state.set_eff_t1(25.0f); state.set_eff_v1(0.97f);
    state.set_eff_t2(55.0f); state.set_eff_v2(0.92f);
    state.set_eff_t3(85.0f); state.set_eff_v3(0.87f);

    // Below t1 → clamp to v1
    CHECK(svc.efficiency_continuous(10.0f) == Approx(0.97f));
    CHECK(svc.efficiency_continuous(20.0f) == Approx(0.97f));
}

TEST_CASE("efficiency clamps above t3", "[gas_model][efficiency]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set custom points
    state.set_eff_t1(25.0f); state.set_eff_v1(0.97f);
    state.set_eff_t2(55.0f); state.set_eff_v2(0.92f);
    state.set_eff_t3(85.0f); state.set_eff_v3(0.87f);

    // Above t3 → clamp to v3
    CHECK(svc.efficiency_continuous(95.0f) == Approx(0.87f));
    CHECK(svc.efficiency_continuous(100.0f) == Approx(0.87f));
}

// ═══════════════════════════════════════════════════════════════
// corrected_calorific with gas_temp_offset
// ═══════════════════════════════════════════════════════════════

TEST_CASE("corrected_calorific uses gas_temp_offset", "[gas_model][cv]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Set offset=0, outdoor=20 → T_gas=20+0=20°C → CV=9.5*288.15/293.15
    state.set_gas_temp_offset(0.0f);
    svc.outdoor_temp_ = 20.0f;
    svc.outdoor_temp_valid_ = true;
    svc.gas_calorific_ = 9.5f;

    float expected = 9.5f * (288.15f / (20.0f + 273.15f));
    CHECK(svc.corrected_calorific() == Approx(expected).margin(0.001f));
}

TEST_CASE("corrected_calorific with default offset gives same result", "[gas_model][cv]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Default offset is -5.0, outdoor=20 → T_gas=15 → CV=9.5*288.15/288.15=9.5
    svc.outdoor_temp_ = 20.0f;
    svc.outdoor_temp_valid_ = true;
    svc.gas_calorific_ = 9.5f;

    float cv = svc.corrected_calorific();
    // At T_gas=15°C (offset=-5, outdoor=20), correction factor = 288.15/288.15 = 1.0
    CHECK(cv == Approx(9.5f).margin(0.001f));
}

TEST_CASE("corrected_calorific offset positive raises CV", "[gas_model][cv]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // offset=+5 (warmer gas), outdoor=0 → T_gas=5°C → CV = 9.5*288.15/278.15
    state.set_gas_temp_offset(5.0f);
    svc.outdoor_temp_ = 0.0f;
    svc.outdoor_temp_valid_ = true;
    svc.gas_calorific_ = 9.5f;

    // T_gas = 0 + 5 = 5C. CV = 9.5 * 288.15 / (5+273.15) = 9.5 * 288.15/278.15
    float expected = 9.5f * (288.15f / 278.15f);
    CHECK(svc.corrected_calorific() == Approx(expected).margin(0.001f));
}

// ═══════════════════════════════════════════════════════════════
// DHW: dynamic param changes
// ═══════════════════════════════════════════════════════════════

TEST_CASE("calc_power respects dhw_pmin after state change", "[gas_model][dhw]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    GasFlowService svc(state, time);

    // Start with defaults — CH and DHW both 5.5/24.0
    svc.dhw_active_ = true;
    CHECK(svc.calc_power(50.0f, 0.0f, 0.0f) == Approx(14.75f).margin(0.001f));

    // Change DHW max — should affect next calc_power
    state.set_dhw_pmax(20.0f);
    // power = 5.5 + (20.0-5.5)*0.5 = 5.5 + 7.25 = 12.75
    CHECK(svc.calc_power(50.0f, 0.0f, 0.0f) == Approx(12.75f).margin(0.001f));
}
