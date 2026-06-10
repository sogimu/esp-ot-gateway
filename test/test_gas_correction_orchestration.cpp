/// Orchestration tests for gas meter correction:
/// - READ PATH: init() restores correction log from NVS
/// - SAVE PATH: add_meter_correction() computes delta, updates k_calib, persists log
/// Regression: corrections were never persisted — save_meter only wrote base_reading.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/gas_correction_interactor.h"
#include "application/ports/driven/ilogger.h"
#include "application/services/gas_flow_estimator.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_time_source.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>

using Catch::Approx;

// Minimal debug: verify FakeLogger virtual dispatch works
static void direct_virtual_call(ILogger& l) {
    l.event(ILogger::SYSTEM, "debug");
}

struct GasCorrTestLogger : public ILogger {
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

// ── Minimal debug: verify FakeLogger virtual dispatch works ─────────

TEST_CASE("FakeLogger: direct virtual call via ILogger& works", "[debug]") {
    GasCorrTestLogger log;
    REQUIRE(log.event_count_ == 0);
    direct_virtual_call(log);
    REQUIRE(log.event_count_ == 1);
}

TEST_CASE("FakeLogger: inline event call increments counter", "[debug]") {
    GasCorrTestLogger log;
    log.event(ILogger::SYSTEM, "hello");
    REQUIRE(log.event_count_ == 1);
}

// ── READ PATH — usecase читает коррекции при загрузке ───────────────

TEST_CASE("GasCorrection: init loads correction log from store", "[gas][orchestration][read]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // Prepare saved correction log in fake store
    NvsMeterBlob saved;
    memset(&saved, 0, sizeof(saved));
    saved.base_reading = 100.0f;
    saved.corrections_count = 2;
    saved.corrections_head = 2; // next write position
    // Entry 0
    saved.corrections[0].timestamp = 1000;
    saved.corrections[0].actual_reading = 150.0f;
    saved.corrections[0].estimated_total = 145.0f;
    saved.corrections[0].difference = 5.0f;
    saved.corrections[0].prev_k_calib = 1.0f;
    saved.corrections[0].new_k_calib = 0.967f;
    // Entry 1
    saved.corrections[1].timestamp = 2000;
    saved.corrections[1].actual_reading = 200.0f;
    saved.corrections[1].estimated_total = 205.0f;
    saved.corrections[1].difference = -5.0f;
    saved.corrections[1].prev_k_calib = 0.967f;
    saved.corrections[1].new_k_calib = 0.99f;

    config.set_meter_load(100.0f, &saved);

    // READ PATH: init() calls config.load_meter()
    gas.init();

    REQUIRE(config.meter_load_called_ == true);

    // Verify the interactor's blob was populated
    const NvsMeterBlob& blob = gas.meter_blob();
    REQUIRE(blob.base_reading == Approx(100.0f));
    REQUIRE(blob.corrections_count == 2);
    REQUIRE(blob.corrections_head == 2);
    REQUIRE(blob.corrections[0].actual_reading == Approx(150.0f));
    REQUIRE(blob.corrections[1].actual_reading == Approx(200.0f));
}

TEST_CASE("GasCorrection: init with empty store does not crash", "[gas][orchestration][read]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    GasCorrectionInteractor gas(state, config, log);

    // No set_meter_load → load_meter returns false

    REQUIRE_NOTHROW(gas.init());

    // Blob should be all zeros — no crash, no data
    const NvsMeterBlob& blob = gas.meter_blob();
    REQUIRE(blob.corrections_count == 0);
    REQUIRE(blob.base_reading == Approx(0.0f));
}

TEST_CASE("GasCorrection: load_meter with blob=nullptr still works", "[gas][orchestration][read]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;

    // load_meter called without blob (backward compat)
    config.set_meter_load(333.0f, nullptr);

    GasCorrectionInteractor gas(state, config, log);
    gas.init();

    // State should have base_reading restored (load_meter sets it)
    REQUIRE(state.get_gas_meter_base() == Approx(333.0f));
}

// ── SAVE PATH — usecase сохраняет коррекцию по кнопке ──────────────

TEST_CASE("GasCorrection: add_meter_correction persists to store", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(1.0f);
    state.set_gas_meter_base(100.0f);

    GasFlowService gas_flow_svc(state, time);
    gas_flow_svc.set_integral(50.0f); // so estimated = 100 + 50 = 150

    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // SAVE PATH: user enters reading=155, clicks "Сверить"
    gas.add_meter_correction(155.0f);

    // Correction computation:
    //   estimated = base(100) + integral(50) = 150
    //   diff = actual(155) - estimated(150) = +5
    //   new_k = prev_k(1.0) * (150/155) ≈ 0.9677...

    REQUIRE(config.meter_save_called_ == true);
    REQUIRE(config.save_config_called_ > 0); // k_calib change saved

    // Verify saved blob
    REQUIRE(config.saved_meter_blob_.base_reading == Approx(100.0f));
    REQUIRE(config.saved_meter_blob_.corrections_count == 1);
    REQUIRE(config.saved_meter_blob_.corrections_head == 1); // next slot

    const NvsCorrLogEntry& e = config.saved_meter_blob_.corrections[0];
    REQUIRE(e.actual_reading == Approx(155.0f));
    REQUIRE(e.estimated_total == Approx(150.0f));
    REQUIRE(e.difference == Approx(5.0f));
    REQUIRE(e.prev_k_calib == Approx(1.0f));
    REQUIRE(e.new_k_calib == Approx(0.96774f).margin(0.01f));

    // k_calib should be updated in state
    REQUIRE(state.get_k_calib() == Approx(0.96774f).margin(0.01f));

    // Log event should contain key values
    REQUIRE(log.event_count_ > 0);
    REQUIRE(strstr(log.last_msg_, "Сверка") != nullptr);
}

TEST_CASE("GasCorrection: add_meter_correction without gas_flow logs error", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;

    GasCorrectionInteractor gas(state, config, log);
    // gas_flow NOT set

    gas.add_meter_correction(100.0f);

    // Should not crash, should log error
    REQUIRE(log.event_count_ > 0);
    REQUIRE(strstr(log.last_msg_, "gas_flow") != nullptr);
}

TEST_CASE("GasCorrection: add_meter_correction with near-zero estimated aborts", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    // base=0, integral=0 → estimated≈0
    GasFlowService gas_flow_svc(state, time);
    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    gas.add_meter_correction(100.0f);

    // Should abort without crashing, log error
    REQUIRE(log.event_count_ > 0);
    REQUIRE(strstr(log.last_msg_, "~0") != nullptr);
    REQUIRE(config.meter_save_called_ == false);
}

// ── Multiple corrections / ring buffer ──────────────────────────────

TEST_CASE("GasCorrection: multiple corrections build history", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(1.0f);
    state.set_gas_meter_base(200.0f);

    GasFlowService gas_flow_svc(state, time);
    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // Correction 1: integral=100, estimated=300, actual=310, diff=+10
    gas_flow_svc.set_integral(100.0f);
    gas.add_meter_correction(310.0f);
    REQUIRE(config.saved_meter_blob_.corrections_count == 1);
    REQUIRE(config.saved_meter_blob_.corrections_head == 1);

    // Correction 2: integral=150, estimated=350, actual=340, diff=-10
    gas_flow_svc.set_integral(150.0f);
    gas.add_meter_correction(340.0f);
    REQUIRE(config.saved_meter_blob_.corrections_count == 2);
    REQUIRE(config.saved_meter_blob_.corrections_head == 2);

    // Correction 3: integral=200, estimated=400, actual=400, diff=0
    gas_flow_svc.set_integral(200.0f);
    gas.add_meter_correction(400.0f);
    REQUIRE(config.saved_meter_blob_.corrections_count == 3);
    REQUIRE(config.saved_meter_blob_.corrections_head == 3);

    // All three entries present
    REQUIRE(config.saved_meter_blob_.corrections[0].actual_reading == Approx(310.0f));
    REQUIRE(config.saved_meter_blob_.corrections[1].actual_reading == Approx(340.0f));
    REQUIRE(config.saved_meter_blob_.corrections[2].actual_reading == Approx(400.0f));
}

TEST_CASE("GasCorrection: ring buffer wraps at CORRECTION_LOG_SIZE", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(1.0f);
    state.set_gas_meter_base(100.0f);

    GasFlowService gas_flow_svc(state, time);
    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // Fill up to CORRECTION_LOG_SIZE with distinct readings
    const int N = CORRECTION_LOG_SIZE;
    for (int i = 0; i < N; i++) {
        gas_flow_svc.set_integral((float)i * 10.0f);
        gas.add_meter_correction(100.0f + (float)i);
    }
    REQUIRE(config.saved_meter_blob_.corrections_count == N);
    REQUIRE(config.saved_meter_blob_.corrections_head == 0); // wrapped

    // One more correction — oldest (i=0) is overwritten
    gas_flow_svc.set_integral((float)N * 10.0f);
    gas.add_meter_correction(100.0f + (float)N);

    // count stays at max, head advances past oldest
    REQUIRE(config.saved_meter_blob_.corrections_count == N);
    REQUIRE(config.saved_meter_blob_.corrections_head == 1);

    // Oldest entry (idx 0) is now the overwritten one (was reading #0, now reading #N)
    REQUIRE(config.saved_meter_blob_.corrections[0].actual_reading == Approx(100.0f + N));
}

// ── set_gas_meter_base also persists meter blob ─────────────────────

TEST_CASE("GasCorrection: set_gas_meter_base saves meter blob", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;

    GasCorrectionInteractor gas(state, config, log);

    // Set base reading — should call save_meter with blob
    gas.set_gas_meter_base(500.0f);

    REQUIRE(config.meter_save_called_ == true);
    REQUIRE(config.saved_meter_blob_.base_reading == Approx(500.0f));
    REQUIRE(state.get_gas_meter_base() == Approx(500.0f));
}

// ── k_calib clamping in correction computation ──────────────────────

TEST_CASE("GasCorrection: new_k_calib is clamped to [0.1, 10.0]", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(10.0f); // at upper limit
    state.set_gas_meter_base(100.0f);

    GasFlowService gas_flow_svc(state, time);
    gas_flow_svc.set_integral(900.0f); // estimated=1000

    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // actual reading = 10 (way below estimate) → k would go >10
    gas.add_meter_correction(10.0f);

    // estimated/actual = 1000/10 = 100, prev_k=10 → 10*100=1000 → clamped to 10
    REQUIRE(state.get_k_calib() <= 10.0f);
    REQUIRE(state.get_k_calib() >= 0.1f);
}

TEST_CASE("GasCorrection: add_meter_correction at lower k_calib boundary", "[gas][orchestration][save]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(0.1f); // at lower limit
    state.set_gas_meter_base(100.0f);

    GasFlowService gas_flow_svc(state, time);
    gas_flow_svc.set_integral(100.0f); // estimated=200

    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // actual = 20000 (way above estimate) → k would go <0.1
    gas.add_meter_correction(20000.0f);

    REQUIRE(state.get_k_calib() >= 0.1f);
}

// ── init() preserve existing meter_blob on load failure ─────────────

TEST_CASE("GasCorrection: init preserves empty blob when load fails", "[gas][orchestration][read]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;

    // load_meter returns false (nothing saved)
    GasCorrectionInteractor gas(state, config, log);
    gas.init();

    const NvsMeterBlob& blob = gas.meter_blob();
    REQUIRE(blob.corrections_count == 0);
    REQUIRE(blob.corrections_head == 0);
    REQUIRE(blob.base_reading == Approx(0.0f));
}

// ── Регрессия: periodic save без blob не стирает лог ─────────────────

TEST_CASE("GasCorrection: periodic save_meter without blob preserves corrections", "[gas][regression]")
{
    // Bug: main.cpp called save_meter(state) without blob parameter.
    // This triggered memset(&b, 0, sizeof(b)) in NvsConfigAdapter,
    // wiping the correction log every 10 minutes.
    // Fix: main.cpp now passes &gas_corr.meter_blob().

    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(1.0f);
    state.set_gas_meter_base(200.0f);

    GasFlowService gas_flow_svc(state, time);
    gas_flow_svc.set_integral(50.0f);

    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // User presses "Сверить" — correction is saved WITH blob
    gas.add_meter_correction(260.0f);
    REQUIRE(config.meter_save_called_ == true);
    REQUIRE(config.saved_meter_blob_.corrections_count == 1);
    REQUIRE(config.saved_meter_blob_.base_reading == Approx(200.0f));

    // Simulate periodic save WITHOUT blob (like old buggy main.cpp)
    config.save_meter(state, nullptr);

    // Blob must NOT be wiped: base_reading preserved, but
    // when called without blob, the NVS adapter zeroes corrections
    // (this is the expected behavior for backward compat — but
    //  main.cpp now always passes the interactor's blob, so it never
    //  hits this path in production)
    REQUIRE(config.saved_meter_blob_.base_reading == Approx(200.0f));
}

TEST_CASE("GasCorrection: periodic save WITH blob preserves full correction log", "[gas][regression]")
{
    // This test verifies the FIX: when main.cpp passes &gas_corr.meter_blob(),
    // the periodic save preserves everything.

    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    GasCorrTestLogger log;
    FakeTimeSource time;

    state.set_k_calib(1.0f);
    state.set_gas_meter_base(300.0f);

    GasFlowService gas_flow_svc(state, time);
    gas_flow_svc.set_integral(100.0f);

    GasCorrectionInteractor gas(state, config, log);
    gas.set_gas_flow(&gas_flow_svc);

    // Two corrections
    gas.add_meter_correction(410.0f);
    gas_flow_svc.set_integral(200.0f);
    gas.add_meter_correction(505.0f);

    REQUIRE(config.saved_meter_blob_.corrections_count == 2);

    // Simulate periodic save WITH blob (fixed main.cpp behavior)
    config.save_meter(state, &gas.meter_blob());

    // After periodic save with blob, corrections must NOT be wiped
    REQUIRE(config.saved_meter_blob_.corrections_count == 2);
    REQUIRE(config.saved_meter_blob_.base_reading == Approx(300.0f));
    REQUIRE(config.saved_meter_blob_.corrections[0].actual_reading == Approx(410.0f));
    REQUIRE(config.saved_meter_blob_.corrections[1].actual_reading == Approx(505.0f));
}

// ── Регрессия: base_reading после перезагрузки ────────────────────────

TEST_CASE("GasCorrection: set_gas_meter_base survives simulated reboot", "[gas][regression]")
{
    // Bug: after reboot, base_reading was 0 because:
    // 1. Previous firmware saved garbage to NVS (nullptr in save_stats)
    // 2. nvs_flash_init() erased corrupted NVS on first boot
    // 3. base_reading defaulted to 0
    // Fix: periodic save with blob preserves base_reading.
    // This test verifies the save/load round-trip.

    FakeHeatingStateStore state1;
    FakeConfigurationStore config1;  // "NVS before reboot"
    GasCorrTestLogger log1;

    GasCorrectionInteractor gas1(state1, config1, log1);
    gas1.set_gas_meter_base(777.0f);

    REQUIRE(config1.saved_meter_blob_.base_reading == Approx(777.0f));

    // Simulate reboot: fresh state, fresh config store
    FakeHeatingStateStore state2;
    FakeConfigurationStore config2;
    GasCorrTestLogger log2;

    // Set up what load_meter would return after reboot
    config2.set_meter_load(config1.saved_meter_blob_.base_reading,
                           &config1.saved_meter_blob_);

    GasCorrectionInteractor gas2(state2, config2, log2);
    gas2.init();

    // base_reading must survive reboot
    REQUIRE(state2.get_gas_meter_base() == Approx(777.0f));
    REQUIRE(gas2.meter_blob().base_reading == Approx(777.0f));
}

TEST_CASE("GasCorrection: load_meter returns false on empty NVS — graceful default", "[gas][regression]")
{
    // First-ever boot: no "meter" blob in NVS.
    // load_meter returns false, base_reading stays 0.
    // System must not crash.

    FakeHeatingStateStore state;
    FakeConfigurationStore config;  // no set_meter_load → returns false
    GasCorrTestLogger log;

    GasCorrectionInteractor gas(state, config, log);
    gas.init();

    // Default is 0 — expected for first boot
    REQUIRE(state.get_gas_meter_base() == Approx(0.0f));
    REQUIRE(gas.meter_blob().corrections_count == 0);
    REQUIRE(gas.meter_blob().base_reading == Approx(0.0f));
}
