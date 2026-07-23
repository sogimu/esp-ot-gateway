#include "application/ports/driven/iheating_stats_store.h"
#include "application/ports/driven/iburn_stats_store.h"
#include "application/ports/driven/igas_correction_store.h"
#include "application/ports/driven/igas_correction_store.h"
/// Integration tests for gas flow estimation, correction, and JSON rendering.
/// Verifies that WebPresenterAdapter::render_stats() produces correct JSON
/// with gas_error_pct, gas_error_monthly_pct, and gas_meter_total fields.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "application/services/gas_flow_estimator.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_time_source.h"
#include "application/ports/driven/ilogger.h"

using Catch::Approx;

// ── Test logger ──────────────────────────────────────────────────────

struct GasIntTestLogger : public ILogger {
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

// ── Helper: check if JSON string contains a key ──────────────────────

static bool json_has_key(const char* json, const char* key) {
    return strstr(json, key) != nullptr;
}

// ══════════════════════════════════════════════════════════════════════
// No corrections — all errors should be zero
// ══════════════════════════════════════════════════════════════════════

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

struct FakeBurnStatsStore : IBurnStatsStore {
    bool load_burn_stats(uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&) override { return false; }
    void save_burn_stats(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
};

struct FakeHeatingStatsStore : IHeatingStatsStore {
    void save_stats(const IHeatingStateStore\&, uint32_t, float, const void*, const void*, const void*, const void*) override {}
    bool load_stats(uint32_t\&, float\&, void*, void*, void*, void*) override { return false; }
    void save_total_uptime(uint32_t) override {}
    bool load_total_uptime(uint32_t\&) override { return false; }
    void save_integral(float) override {}
    void save_meter(const IHeatingStateStore\&, const void*) override {}
    bool load_meter(IHeatingStateStore\&, void*) override { return false; }
};

TEST_CASE("no corrections gives zero error pct", "[integration][gas]")
{
    // Without calling add_meter_correction, gas_error_pct must be 0.

    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService gas_flow(state, time, hss);
    FakeConfigurationStore config;
    FakeGasStore gas_store;
    gas_store.cfg = &config;
GasIntTestLogger log;
    GasCorrectionInteractor gas_corr(state, gas_store, log);

    ModulationStatsService mod_stats(state);
    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_mod_stats(&mod_stats);
    presenter.set_burn_cycles(&burn_cycles);
    presenter.set_gas_flow(&gas_flow);
    presenter.set_gas_correction(&gas_corr);
    presenter.set_time_source(&time);

    char buf[4096] = {};
    presenter.render_stats(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    CHECK(json_has_key(buf, "\"gas_error_pct\""));
    CHECK(strstr(buf, "\"gas_error_pct\":0.0") != nullptr);
}

// ══════════════════════════════════════════════════════════════════════
// gas_meter_total equals base + integral
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("gas_meter_total equals base + integral", "[integration][gas]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService gas_flow(state, time, hss);
    FakeConfigurationStore config;
    FakeGasStore gas_store;
    gas_store.cfg = &config;
GasIntTestLogger log;
    GasCorrectionInteractor gas_corr(state, gas_store, log);

    ModulationStatsService mod_stats(state);
    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);

    // Set base reading and integral
    state.set_gas_meter_base(1000.0f);
    gas_flow.set_integral(12.345f);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_mod_stats(&mod_stats);
    presenter.set_burn_cycles(&burn_cycles);
    presenter.set_gas_flow(&gas_flow);
    presenter.set_gas_correction(&gas_corr);
    presenter.set_time_source(&time);

    char buf[4096] = {};
    presenter.render_stats(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    // gas_meter_total = 1000.0 + 12.345 = 1012.345
    CHECK(json_has_key(buf, "\"gas_meter_total\""));
    // Check for the computed value
    CHECK(strstr(buf, "\"gas_meter_total\":1012.345") != nullptr);
    CHECK(strstr(buf, "\"gas_meter_base\":1000.000") != nullptr);
}

// ══════════════════════════════════════════════════════════════════════
// JSON keys present in render_stats output
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("JSON stats contains gas_error_pct with 2 corrections", "[integration][gas][json]")
{
    // gas_error_pct = |diff| / actual_consumed * 100
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService gas_flow(state, time, hss);
    FakeConfigurationStore config;
    FakeGasStore gas_store;
    gas_store.cfg = &config;
GasIntTestLogger log;
    GasCorrectionInteractor gas_corr(state, gas_store, log);

    ModulationStatsService mod_stats(state);
    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);

    // Correction 1: base=100, integral=50 → estimated=150, actual=200, diff=50
    state.set_gas_meter_base(100.0f);
    state.set_k_calib(1.0f);
    gas_corr.set_gas_flow(&gas_flow);
    gas_corr.set_time_source(&time);
    gas_flow.set_integral(50.0f);
    gas_corr.add_meter_correction(200.0f);
    // Correction 2: 1 day later, integral=100, actual=300
    // actual_consumed = 300-200 = 100, estimated_consumed = 100+100-200 = 0? No...
    // after corr1: base=200, integral=0. Then integral accumulates to 100.
    // estimated_total at corr2 = 200 + 100 = 300. diff = 300-300 = 0. error=0%.
    time.advance_sec(86400);
    gas_flow.set_integral(100.0f);
    gas_corr.add_meter_correction(300.0f);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_mod_stats(&mod_stats);
    presenter.set_burn_cycles(&burn_cycles);
    presenter.set_gas_flow(&gas_flow);
    presenter.set_gas_correction(&gas_corr);
    presenter.set_time_source(&time);

    char buf[4096] = {};
    presenter.render_stats(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    CHECK(json_has_key(buf, "\"gas_error_pct\""));
}

TEST_CASE("gas_error_pct with real error scenario", "[integration][gas][json]")
{
    // Simulate: estimated underestimates consumption
    // Corr1: actual=104, estimated=104 (perfect match, diff=0)
    // Corr2: actual=108, estimated=107 (diff=1, actual_consumed=4)
    // error_pct = 1/4*100 = 25%
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService gas_flow(state, time, hss);
    FakeConfigurationStore config;
    FakeGasStore gas_store;
    gas_store.cfg = &config;
GasIntTestLogger log;
    GasCorrectionInteractor gas_corr(state, gas_store, log);

    ModulationStatsService mod_stats(state);
    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);

    state.set_gas_meter_base(100.0f);
    state.set_k_calib(1.0f);
    gas_corr.set_gas_flow(&gas_flow);
    gas_corr.set_time_source(&time);

    // Correction 1: perfect match (actual = base + integral)
    // integral must be >= 0.001 — correction requires real consumption
    gas_flow.set_integral(4.0f);
    gas_corr.add_meter_correction(104.0f);

    // Correction 2: 2 days later, estimated=104+3=107, actual=108
    // actual_consumed=4, estimated_consumed=3, error=25%
    time.advance_sec(172800);
    gas_flow.set_integral(3.0f);
    gas_corr.add_meter_correction(108.0f);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_mod_stats(&mod_stats);
    presenter.set_burn_cycles(&burn_cycles);
    presenter.set_gas_flow(&gas_flow);
    presenter.set_gas_correction(&gas_corr);
    presenter.set_time_source(&time);

    char buf[4096] = {};
    presenter.render_stats(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    CHECK(json_has_key(buf, "\"gas_error_pct\""));
    // error_pct = |1| / (104-100) * 100 = 1/4*100 = 25%
    CHECK(strstr(buf, "\"gas_error_pct\":25.0") != nullptr);
}

// ══════════════════════════════════════════════════════════════════════
// render_status contains outside_temp
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("render_status contains outside_temp", "[integration][status][json]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_time_source(&time);

    // Set a known outdoor temperature
    state.set_outside_temp(-5.2f);

    char buf[2048] = {};
    presenter.render_status(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    CHECK(json_has_key(buf, "\"outside_temp\""));
    CHECK(strstr(buf, "\"outside_temp\":-5.2") != nullptr);
}

// ══════════════════════════════════════════════════════════════════════
// render_stats contains gas_temp_offset and boiler model fields
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("render_stats contains gas_temp_offset and model fields", "[integration][stats][json]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService gas_flow(state, time, hss);
    FakeConfigurationStore config;
    FakeGasStore gas_store;
    gas_store.cfg = &config;
GasIntTestLogger log;
    GasCorrectionInteractor gas_corr(state, gas_store, log);

    ModulationStatsService mod_stats(state);
    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);

    // Set boiler model values
    state.set_gas_temp_offset(-5.0f);
    state.set_ch_pmin(5.5f);
    state.set_ch_pmax(24.0f);
    state.set_dhw_pmin(5.5f);
    state.set_dhw_pmax(24.0f);
    state.set_eff_t1(30.0f);
    state.set_eff_v1(0.98f);
    state.set_eff_t2(55.0f);
    state.set_eff_v2(0.93f);
    state.set_eff_t3(80.0f);
    state.set_eff_v3(0.88f);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_mod_stats(&mod_stats);
    presenter.set_burn_cycles(&burn_cycles);
    presenter.set_gas_flow(&gas_flow);
    presenter.set_gas_correction(&gas_corr);
    presenter.set_time_source(&time);

    char buf[4096] = {};
    presenter.render_stats(buf, sizeof(buf));

    INFO("JSON output: " << buf);

    CHECK(json_has_key(buf, "\"gas_temp_offset\""));
    CHECK(json_has_key(buf, "\"ch_pmin\""));
    CHECK(json_has_key(buf, "\"ch_pmax\""));
    CHECK(json_has_key(buf, "\"dhw_pmin\""));
    CHECK(json_has_key(buf, "\"dhw_pmax\""));
    CHECK(json_has_key(buf, "\"eff_t1\""));
    CHECK(json_has_key(buf, "\"eff_v1\""));
    CHECK(json_has_key(buf, "\"eff_t2\""));
    CHECK(json_has_key(buf, "\"eff_v2\""));
    CHECK(json_has_key(buf, "\"eff_t3\""));
    CHECK(json_has_key(buf, "\"eff_v3\""));
    // Verify values
    CHECK(strstr(buf, "\"gas_temp_offset\":-5.0") != nullptr);
    CHECK(strstr(buf, "\"eff_t1\":30") != nullptr);
    CHECK(strstr(buf, "\"eff_v1\":0.98") != nullptr);
}
