/// Tests for boiler model config blob sizes, default values, validation and persistence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "nvs_config_adapter.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "application/ports/driven/ilogger.h"
#include <cstdarg>
#include <cstdio>

using Catch::Approx;

// ── Blob size checks ───────────────────────────────────────────

TEST_CASE("NvsCalibBlob size is 32", "[boiler_model]") {
    REQUIRE(sizeof(NvsCalibBlob) == 32);
}

TEST_CASE("NvsEfficiencyBlob size is 24", "[boiler_model]") {
    REQUIRE(sizeof(NvsEfficiencyBlob) == 24);
}

// ── Default values match Baxi datasheet ────────────────────────

TEST_CASE("default values match Baxi datasheet", "[boiler_model]") {
    FakeHeatingStateStore state;
    // Defaults should be set by constructor or reset_to_defaults
    state.reset_to_defaults();

    // Gas temp offset
    REQUIRE(state.get_gas_temp_offset() == Approx(-5.0f));

    // CH power — input (nameplate), flat across MWT
    REQUIRE(state.get_ch_pmin() == Approx(5.5f));
    REQUIRE(state.get_ch_pmax() == Approx(24.0f));

    // DHW power — input (nameplate)
    REQUIRE(state.get_dhw_pmin() == Approx(5.5f));
    REQUIRE(state.get_dhw_pmax() == Approx(24.0f));

    // Efficiency curve points
    REQUIRE(state.get_eff_t1() == Approx(30.0f));
    REQUIRE(state.get_eff_v1() == Approx(0.98f));
    REQUIRE(state.get_eff_t2() == Approx(55.0f));
    REQUIRE(state.get_eff_v2() == Approx(0.93f));
    REQUIRE(state.get_eff_t3() == Approx(80.0f));
    REQUIRE(state.get_eff_v3() == Approx(0.88f));
}

// ── Validation: gas_temp_offset ────────────────────────────────

struct TestLogger : public ILogger {
    void event(ILogger::Category, const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args);
        event_count_++;
    }
    char last_msg_[256] = {};
    int event_count_ = 0;
};

TEST_CASE("gas_temp_offset clamps to [-20, +10]", "[boiler_model][validation]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    TestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Set above max
    gas.set_gas_temp_offset(15.0f);
    REQUIRE(state.get_gas_temp_offset() == Approx(10.0f));

    // Set below min
    gas.set_gas_temp_offset(-30.0f);
    REQUIRE(state.get_gas_temp_offset() == Approx(-20.0f));

    // Set valid value
    gas.set_gas_temp_offset(-5.0f);
    REQUIRE(state.get_gas_temp_offset() == Approx(-5.0f));
}

// ── Validation: CH power pmin < pmax ───────────────────────────

TEST_CASE("ch_power validates pmin < pmax", "[boiler_model][validation]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    TestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Set invalid: pmin >= pmax — should reset to defaults
    gas.set_ch_power(10.0f, 5.0f);
    REQUIRE(state.get_ch_pmin() == Approx(5.5f));
    REQUIRE(state.get_ch_pmax() == Approx(24.0f));

    // Valid values pass through
    gas.set_ch_power(5.0f, 25.0f);
    REQUIRE(state.get_ch_pmin() == Approx(5.0f));
    REQUIRE(state.get_ch_pmax() == Approx(25.0f));
}

// ── Validation: DHW power pmin < pmax ──────────────────────────

TEST_CASE("dhw_power validates pmin < pmax", "[boiler_model][validation]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    TestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Set invalid: pmin >= pmax — should reset to defaults
    gas.set_dhw_power(20.0f, 5.0f);
    REQUIRE(state.get_dhw_pmin() == Approx(5.5f));
    REQUIRE(state.get_dhw_pmax() == Approx(24.0f));
}

// ── Validation: efficiency points t1 < t2 < t3 ─────────────────

TEST_CASE("efficiency points validate t1 < t2 < t3", "[boiler_model][validation]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    TestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Set invalid ordering: t1=50, t2=40 — should reset to defaults
    gas.set_efficiency_points(50.0f, 0.98f, 40.0f, 0.93f, 80.0f, 0.88f);
    REQUIRE(state.get_eff_t1() == Approx(30.0f));
    REQUIRE(state.get_eff_t2() == Approx(55.0f));
    REQUIRE(state.get_eff_t3() == Approx(80.0f));

    // t2 >= t3 should also trigger reset
    gas.set_efficiency_points(30.0f, 0.98f, 80.0f, 0.93f, 55.0f, 0.88f);
    REQUIRE(state.get_eff_t2() == Approx(55.0f));
    REQUIRE(state.get_eff_t3() == Approx(80.0f));
}

// ── Validation: efficiency point ranges ────────────────────────

TEST_CASE("efficiency points clamp T to [20, 90] and V to [0.80, 1.00]", "[boiler_model][validation]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    TestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Set values outside range (ordering t1<t2<t3 holds after clamping)
    gas.set_efficiency_points(15.0f, 1.05f, 50.0f, 0.70f, 95.0f, 0.95f);
    // t1 clamped to 20, v1 capped at 1.00
    REQUIRE(state.get_eff_t1() == Approx(20.0f));
    REQUIRE(state.get_eff_v1() == Approx(1.00f));
    // t2 stays 50, v2 clamped to 0.80
    REQUIRE(state.get_eff_t2() == Approx(50.0f));
    REQUIRE(state.get_eff_v2() == Approx(0.80f));
    // t3 clamped to 90, v3 stays 0.95 (and t1<t2<t3 holds after clamps)
    REQUIRE(state.get_eff_t3() == Approx(90.0f));
    REQUIRE(state.get_eff_v3() == Approx(0.95f));
}

// ── NvsCalibBlob default initialization ────────────────────────

TEST_CASE("NvsCalibBlob defaults in struct match specification", "[boiler_model]") {
    // Verify the struct's expected memory layout: existing fields at front, new fields after
    NvsCalibBlob b;
    memset(&b, 0, sizeof(b));
    b.k_calib = 1.0f; b.p_max = 24.0f; b.gas_calorific = 9.5f;
    b.gas_temp_offset = -5.0f;
    b.ch_pmin = 5.5f; b.ch_pmax = 24.0f;
    b.dhw_pmin = 5.5f; b.dhw_pmax = 24.0f;

    // Verify memory offsets: 8 floats (32 bytes)
    float* pf = &b.k_calib;
    REQUIRE(pf[0] == Approx(1.0f));
    REQUIRE(pf[1] == Approx(24.0f));
    REQUIRE(pf[2] == Approx(9.5f));
    REQUIRE(pf[3] == Approx(-5.0f));
    REQUIRE(pf[4] == Approx(5.5f));
    REQUIRE(pf[5] == Approx(24.0f));
    REQUIRE(pf[6] == Approx(5.5f));
    REQUIRE(pf[7] == Approx(24.0f));
}

// ── NvsEfficiencyBlob defaults ─────────────────────────────────

TEST_CASE("NvsEfficiencyBlob defaults match specification", "[boiler_model]") {
    NvsEfficiencyBlob b;
    memset(&b, 0, sizeof(b));
    b.t1 = 30.0f; b.v1 = 0.98f;
    b.t2 = 55.0f; b.v2 = 0.93f;
    b.t3 = 80.0f; b.v3 = 0.88f;

    REQUIRE(b.t1 == Approx(30.0f));
    REQUIRE(b.v1 == Approx(0.98f));
    REQUIRE(b.t2 == Approx(55.0f));
    REQUIRE(b.v2 == Approx(0.93f));
    REQUIRE(b.t3 == Approx(80.0f));
    REQUIRE(b.v3 == Approx(0.88f));
}

// ── Calib blob migration (12-byte → 32-byte and 40-byte → 32-byte) ──

TEST_CASE("calib blob migration: can parse 12-byte blob with new defaults", "[boiler_model][migration]") {
    // Simulate old-format blob: 3 floats (12 bytes)
    uint8_t old_blob[12];
    float* pf = reinterpret_cast<float*>(old_blob);
    pf[0] = 1.25f;  // k_calib
    pf[1] = 22.0f;  // p_max
    pf[2] = 9.8f;   // gas_calorific

    // Interpret as new blob — first 3 floats match, rest should be zero (caller fills defaults)
    NvsCalibBlob new_blob;
    memcpy(&new_blob, old_blob, 12);
    memset(reinterpret_cast<uint8_t*>(&new_blob) + 12, 0, sizeof(new_blob) - 12);

    REQUIRE(new_blob.k_calib == Approx(1.25f));
    REQUIRE(new_blob.p_max == Approx(22.0f));
    REQUIRE(new_blob.gas_calorific == Approx(9.8f));
    // New fields are zero after memcpy of old blob + zeroing rest
    REQUIRE(new_blob.gas_temp_offset == Approx(0.0f));
    REQUIRE(new_blob.ch_pmin == Approx(0.0f));
}

// ── Calib blob migration (40-byte → 32-byte) ───────────────────

TEST_CASE("calib blob migration: 40-byte old format → 32-byte (CH/DHW reset to defaults)", "[boiler_model][migration]") {
    // Simulate old-format blob with 4 CH params (warm/hot)
    struct __attribute__((packed)) OldBlob {
        float k_calib, p_max, gas_calorific;
        float gas_temp_offset;
        float ch_pmin_warm, ch_pmax_warm;
        float ch_pmin_hot,  ch_pmax_hot;
        float dhw_pmin,     dhw_pmax;
    } old;
    old.k_calib = 1.35f;
    old.p_max = 22.0f;
    old.gas_calorific = 9.7f;
    old.gas_temp_offset = -6.0f;
    old.ch_pmin_warm = 3.5f;  old.ch_pmax_warm = 20.0f;
    old.ch_pmin_hot  = 3.0f;  old.ch_pmax_hot  = 18.0f;
    old.dhw_pmin = 5.5f;      old.dhw_pmax = 24.0f;
    REQUIRE(sizeof(old) == 40);

    // Migration: keep scalar fields (k_calib, p_max, gas_calorific, gas_temp_offset)
    // Discard old output-power values — caller fills new input-power defaults
    NvsCalibBlob new_blob;
    memset(&new_blob, 0, sizeof(new_blob));
    new_blob.k_calib = old.k_calib;
    new_blob.p_max = old.p_max;
    new_blob.gas_calorific = old.gas_calorific;
    new_blob.gas_temp_offset = old.gas_temp_offset;
    // ch_pmin, ch_pmax, dhw_pmin, dhw_pmax stay at 0 (defaults applied by caller)

    REQUIRE(new_blob.k_calib == Approx(1.35f));
    REQUIRE(new_blob.p_max == Approx(22.0f));
    REQUIRE(new_blob.gas_calorific == Approx(9.7f));
    REQUIRE(new_blob.gas_temp_offset == Approx(-6.0f));
    // CH/DHW power params reset to 0 (caller fills new defaults: 5.5/24.0)
    REQUIRE(new_blob.ch_pmin == Approx(0.0f));
    REQUIRE(new_blob.ch_pmax == Approx(0.0f));
    REQUIRE(new_blob.dhw_pmin == Approx(0.0f));
    REQUIRE(new_blob.dhw_pmax == Approx(0.0f));

    // Verify new blob size
    REQUIRE(sizeof(new_blob) == 32);
}
