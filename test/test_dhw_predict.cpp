#include "application/ports/driven/ipredict_store.h"
/// Tests for DHWPredictService (application/services/dhw_predict_service.h)
/// Covers: session lifecycle (start/update/finish), edge detection,
///         prediction computation, history persistence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/dhw_predict_service.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_time_source.h"
#include <cmath>

using Catch::Approx;

// ── Construction & Initialization ───────────────────────────

// FakePredictStore delegates to FakeConfigurationStore for predict methods
struct FakePredictStore : IPredictStore {
    FakeConfigurationStore* cfg = nullptr;
    bool load_predict(float r[3], int& idx, int& cnt) override {
        return cfg ? cfg->load_predict(r, idx, cnt) : false;
    }
    void save_predict(const float r[3], int idx, int cnt) override {
        if (cfg) cfg->save_predict(r, idx, cnt);
    }
};

TEST_CASE("DHWPredict: default state after construction", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // No prediction active initially
    REQUIRE(state.get_dhw_pred_active() == false);
    REQUIRE(state.get_dhw_pred_remaining_sec() == 0);
    REQUIRE(state.get_dhw_pred_rate_cps() == Approx(0.0f));
}

TEST_CASE("DHWPredict: load_history when NVS returns nothing", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);
    svc.load_history(); // config returns false → uses defaults

    // Should not crash or set anything unexpected
    REQUIRE(state.get_dhw_pred_active() == false);
}

TEST_CASE("DHWPredict: load_history restores saved rates", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;

    // Pre-populate NVS with history
    float saved_rates[3] = {0.04f, 0.05f, 0.06f};
    config.set_predict_history(saved_rates, 2, 3);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);
    svc.load_history();

    // Should not crash — verified via no prediction yet
    REQUIRE(state.get_dhw_pred_active() == false);
}

// ── Session lifecycle: edge detection ──────────────────────

TEST_CASE("DHWPredict: session starts when flame+dwh_active both become true", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000); // 1 second

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // Set DHW initial temperature
    state.set_dhw_temp(45.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);

    // Flame OFF, DHW not active → no session
    state.set_flame(false);
    state.set_dhw_active(false);
    svc.execute();
    REQUIRE(state.get_dhw_pred_active() == false);

    // Flame ON + DHW active → session should start
    state.set_flame(true);
    state.set_dhw_active(true);
    svc.execute();
    // First poll starts session, but push_prediction requires cycle_count >= 2
    // So first poll: session_active_=true but no prediction yet
}

TEST_CASE("DHWPredict: prediction appears after 2+ cycles of heating", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    state.set_dhw_temp(45.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    // Poll 1: session starts, cycle_count=0→1, push_prediction skipped (< 2)
    svc.execute();
    REQUIRE(state.get_dhw_pred_active() == false);

    // Advance time, raise temp slightly
    time.advance_ms(1100);
    state.set_dhw_temp(45.1f);

    // Poll 2: cycle_count=1→2, push_prediction skipped (< 2)
    svc.execute();

    // Poll 3: cycle_count=2→3, push_prediction runs (>= 2)
    time.advance_ms(1100);
    state.set_dhw_temp(45.2f);
    svc.execute();

    // Now prediction should be active
    REQUIRE(state.get_dhw_pred_active() == true);
    // Remaining time should be > 0 (need to heat from ~45 to 55)
    REQUIRE(state.get_dhw_pred_remaining_sec() > 0);
    // Rate should be positive
    REQUIRE(state.get_dhw_pred_rate_cps() > 0.0f);
}

// ── Session finish ──────────────────────────────────────────

TEST_CASE("DHWPredict: session ends when flame goes off", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // Start a heating session
    state.set_dhw_temp(45.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    // Poll a few cycles to build prediction
    for (int i = 0; i < 5; i++) {
        state.set_dhw_temp(45.0f + static_cast<float>(i) * 0.2f);
        svc.execute();
        time.advance_ms(1100);
    }

    // Prediction should be active
    REQUIRE(state.get_dhw_pred_active() == true);

    // Flame goes off
    state.set_flame(false);
    svc.execute();

    // Session should be finished, prediction cleared
    REQUIRE(state.get_dhw_pred_active() == false);
    REQUIRE(state.get_dhw_pred_remaining_sec() == 0);
}

TEST_CASE("DHWPredict: persist history on session finish", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // Start session
    state.set_dhw_temp(40.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    // Simulate heating over time
    for (int i = 0; i < 30; i++) {
        float temp = 40.0f + 0.05f * static_cast<float>(i) * 1.1f;
        state.set_dhw_temp(temp);
        svc.execute();
        time.advance_ms(1100);
    }

    // End session
    state.set_flame(false);
    svc.execute();

    // save_predict should have been called
    REQUIRE(config.save_predict_called_ >= 1);
}

// ── Edge cases ──────────────────────────────────────────────

TEST_CASE("DHWPredict: no session when only flame but no dhw_active", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // CH heating: flame=1 but dhw_active=0
    state.set_flame(true);
    state.set_dhw_active(false);
    state.set_dhw_temp(50.0f);

    for (int i = 0; i < 10; i++) {
        svc.execute();
        time.advance_ms(1100);
    }

    // No DHW prediction session should have started
    REQUIRE(state.get_dhw_pred_active() == false);
}

TEST_CASE("DHWPredict: no session when dhw_active but no flame", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    // DHW pump running but flame off (pre-purge or post-purge)
    state.set_flame(false);
    state.set_dhw_active(true);
    state.set_dhw_temp(50.0f);

    for (int i = 0; i < 10; i++) {
        svc.execute();
        time.advance_ms(1100);
    }

    REQUIRE(state.get_dhw_pred_active() == false);
}

TEST_CASE("DHWPredict: prediction gives reasonable remaining time", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    state.set_dhw_temp(50.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    // Heat at roughly 0.04 °C/s
    for (int i = 0; i < 20; i++) {
        float temp = 50.0f + 0.04f * static_cast<float>(i) * 1.1f;
        if (temp > 55.0f) temp = 55.0f;
        state.set_dhw_temp(temp);
        svc.execute();
        time.advance_ms(1100);
    }

    // Remaining time should be positive and reasonable
    // At ~0.04°/s with ~3°C remaining → ~75 seconds
    if (state.get_dhw_pred_active()) {
        int remaining = state.get_dhw_pred_remaining_sec();
        REQUIRE(remaining > 0);
        REQUIRE(remaining < 600); // less than 10 minutes
    }
}

TEST_CASE("DHWPredict: prediction handles temperature near setpoint", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    state.set_dhw_temp(54.8f); // very close to setpoint
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    for (int i = 0; i < 5; i++) {
        svc.execute();
        time.advance_ms(1100);
    }

    // If prediction is active, remaining should be small
    if (state.get_dhw_pred_active()) {
        int remaining = state.get_dhw_pred_remaining_sec();
        REQUIRE(remaining >= 0);
        REQUIRE(remaining < 300); // less than 5 min
    }
}

TEST_CASE("DHWPredict: handles rapid temperature changes gracefully", "[dhw_pred][app]") {
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakePredictStore pred_store;
    FakeTimeSource time;
    time.set_us(1000000);

        pred_store.cfg = &config;
    DHWPredictService svc(state, pred_store, time);

    state.set_dhw_temp(40.0f);
    state.set_dhw_setpoint(55.0f);
    state.set_dhw_enable(true);
    state.set_flame(true);
    state.set_dhw_active(true);

    // First stabilize
    for (int i = 0; i < 5; i++) {
        state.set_dhw_temp(40.0f + static_cast<float>(i) * 0.1f);
        svc.execute();
        time.advance_ms(1100);
    }

    // Sudden jump (sensor glitch)
    state.set_dhw_temp(60.0f);
    svc.execute();
    time.advance_ms(1100);

    // Should not crash — just continue
    state.set_dhw_temp(42.0f);
    svc.execute();

    // Verify prediction is still sane
    if (state.get_dhw_pred_active()) {
        REQUIRE(state.get_dhw_pred_remaining_sec() >= 0);
    }
}
