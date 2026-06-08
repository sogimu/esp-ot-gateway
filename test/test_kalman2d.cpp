/// Tests for Kalman2D filter (domain/services/kalman2d.h)
/// Covers: initialization, convergence, prediction, edge cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/services/kalman2d.h"
#include <cmath>

using Catch::Approx;

// ── Construction & Initialization ───────────────────────────

TEST_CASE("Kalman2D: default construction", "[kalman2d][domain]") {
    Kalman2D k;
    // Default constructed with reset(0, 0.03f)
    REQUIRE(k.temperature() == Approx(0.0f));
    REQUIRE(k.rate() == Approx(0.03f));
}

TEST_CASE("Kalman2D: reset with values", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(50.0f, 0.05f);

    REQUIRE(k.temperature() == Approx(50.0f));
    REQUIRE(k.rate() == Approx(0.05f));
}

// ── First update (initialization from measurement) ───────────

TEST_CASE("Kalman2D: first update initializes filter", "[kalman2d][domain]") {
    Kalman2D k;
    float result = k.update(45.0f, 1000.0f);  // 1 second

    // First update: filter blends measurement with prior (0, 0.03)
    // Temperature converges quickly (high P00 → high K0)
    REQUIRE(result == Approx(45.0f).margin(0.1f));
    REQUIRE(k.temperature() == Approx(45.0f).margin(0.1f));
    // Rate is unreliable after single update — skip rate check (converges later)
}

// ── Steady-state convergence ────────────────────────────────

TEST_CASE("Kalman2D: converges to constant measurement", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(20.0f, 0.03f);

    float t_ms = 0;
    // Feed constant temperature for many cycles
    for (int i = 0; i < 100; i++) {
        t_ms += 1100.0f;  // 1.1s per cycle (matches poll period)
        k.update(20.0f, t_ms);
    }

    // After many updates, should converge to 20.0
    REQUIRE(k.temperature() == Approx(20.0f).margin(0.1f));
    // Rate should converge to near-zero (constant temp)
    REQUIRE(std::abs(k.rate()) < 0.01f);
}

TEST_CASE("Kalman2D: tracks linear ramp", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(40.0f, 0.03f);

    float t_ms = 0;
    // Ramp: 0.05 °C/s rise
    for (int i = 0; i < 120; i++) {
        t_ms += 1100.0f;
        float true_temp = 40.0f + 0.05f * (t_ms / 1000.0f);
        k.update(true_temp, t_ms);
    }

    // After 120 cycles (~2 min), should track the ramp
    float final_time_s = t_ms / 1000.0f;
    float expected = 40.0f + 0.05f * final_time_s;
    REQUIRE(k.temperature() == Approx(expected).margin(0.5f));
    // Rate estimate should be close to 0.05 °C/s
    REQUIRE(k.rate() == Approx(0.05f).margin(0.02f));
}

// ── Prediction ──────────────────────────────────────────────

TEST_CASE("Kalman2D: predict forward in time", "[kalman2d][domain]") {
    Kalman2D k;
    // Set known state: temp=50, rate=0.05 °C/s
    k.reset(50.0f, 0.05f);

    // After last update at t=1000ms, predict at t=1100ms (100ms later)
    k.update(50.0f, 1000.0f);
    float predicted = k.predict(1100.0f);

    // 0.1s * 0.05 °C/s = 0.005 °C rise
    REQUIRE(predicted == Approx(50.005f).margin(0.01f));
}

TEST_CASE("Kalman2D: predict long horizon", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(45.0f, 0.04f);

    // Let the filter converge on a steady ramp (45°C rising at 0.04 °C/s)
    float t_ms = 0;
    for (int i = 0; i < 30; i++) {
        t_ms += 1100.0f;
        float true_temp = 45.0f + 0.04f * (t_ms / 1000.0f);
        k.update(true_temp, t_ms);
    }

    // Predict 5 minutes ahead (300 seconds)
    float predicted = k.predict(t_ms + 300000.0f);
    float current_temp = k.temperature();
    float rate = k.rate();
    float expected = current_temp + rate * 300.0f;
    REQUIRE(predicted == Approx(expected).margin(2.0f));
}

// ── Edge cases ──────────────────────────────────────────────

TEST_CASE("Kalman2D: handles very short dt (clamped to 1ms)", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(50.0f, 0.03f);

    // Two updates with essentially zero time delta
    k.update(50.0f, 1000.0f);
    float result = k.update(50.1f, 1000.001f); // 1us delta → clamped to 1ms

    // Should not explode — filter handles this gracefully
    REQUIRE(std::isfinite(result));
    REQUIRE(std::isfinite(k.temperature()));
    REQUIRE(std::isfinite(k.rate()));
}

TEST_CASE("Kalman2D: handles very long dt (clamped to 60s)", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(50.0f, 0.03f);

    k.update(50.0f, 1000.0f);
    float result = k.update(55.0f, 100000000.0f); // huge dt → clamped to 60s

    // Should not produce NaN or huge values
    REQUIRE(std::isfinite(result));
    REQUIRE(std::abs(k.temperature()) < 200.0f);
}

TEST_CASE("Kalman2D: uncertainty decreases with more data", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(40.0f, 0.03f);

    float t_ms = 0;
    float prev_unc = k.uncertainty();
    int unc_decreases = 0;

    for (int i = 0; i < 50; i++) {
        t_ms += 1100.0f;
        k.update(40.0f + 0.03f * (t_ms / 1000.0f), t_ms);
        float cur_unc = k.uncertainty();
        if (cur_unc < prev_unc) unc_decreases++;
        prev_unc = cur_unc;
    }

    // Uncertainty should generally decrease with more data
    REQUIRE(unc_decreases > 20);
}

TEST_CASE("Kalman2D: re-reset works correctly", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(30.0f, 0.05f);
    k.update(32.0f, 1000.0f);
    k.update(34.0f, 2000.0f);

    // Re-reset
    k.reset(50.0f, 0.02f);
    REQUIRE(k.temperature() == Approx(50.0f));
    REQUIRE(k.rate() == Approx(0.02f));
}

// ── DHW heating scenario (real-world) ──────────────────────

TEST_CASE("Kalman2D: typical DHW heating curve", "[kalman2d][domain]") {
    Kalman2D k;
    // DHW heating: starts at 40°C, heats at ~0.05 °C/s to 55°C
    k.reset(40.0f, 0.03f); // prior from history

    float t_ms = 0;
    float true_temp = 40.0f;
    float rate = 0.048f;  // actual heating rate

    // Simulate 5 minutes of heating with 1.1s cycles
    int cycles = static_cast<int>(300.0f / 1.1f);
    float final_est = 0;
    float final_rate = 0;

    for (int i = 0; i < cycles; i++) {
        t_ms += 1100.0f;
        true_temp = 40.0f + rate * (t_ms / 1000.0f);
        final_est = k.update(true_temp, t_ms);
        final_rate = k.rate();
    }

    // After 5 min, prediction should be close to true temp
    REQUIRE(final_est == Approx(true_temp).margin(1.0f));
    // Rate estimate should be reasonable
    REQUIRE(final_rate == Approx(rate).margin(0.03f));
    REQUIRE(final_rate > 0.01f);
}

TEST_CASE("Kalman2D: predict remaining time to setpoint", "[kalman2d][domain]") {
    Kalman2D k;
    k.reset(45.0f, 0.05f);

    float t_ms = 0;
    // Heat from 45 to 55 at ~0.05 °C/s
    for (int i = 0; i < 100; i++) {
        t_ms += 1100.0f;
        float tt = 45.0f + 0.05f * (t_ms / 1000.0f);
        if (tt > 55.0f) tt = 55.0f;
        k.update(tt, t_ms);
    }

    float rate = k.rate();
    float temp = k.temperature();
    float delta = 55.0f - temp;
    float remaining_s = (delta > 0 && rate > 0.002f) ? delta / rate : 0;

    REQUIRE(rate > 0.01f);
    REQUIRE(remaining_s >= 0);
    REQUIRE(remaining_s < 1000.0f); // should be reasonable
}
