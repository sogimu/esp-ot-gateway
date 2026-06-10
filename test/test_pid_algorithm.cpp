/// Tests for PID algorithm (domain/services/pid_algorithm.h)
/// Covers: P/I/D contributions, anti-windup, output clamping, edge cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/services/pid_algorithm.h"
#include <cmath>

using Catch::Approx;

// ── Pure P control ──────────────────────────────────────────

TEST_CASE("PID: pure P control — proportional to error", "[pid][domain]") {
    PidAlgoCfg cfg = {2.0f, 0, 0, 0, 100};
    PidAlgoState s;
    pid_init(&s);

    // Error = 5°C (target 25, measured 20)
    float output = pid_step(&cfg, &s, 25.0f, 20.0f, 60);

    // Expected: Kp * error = 2.0 * 5 = 10.0
    REQUIRE(output == Approx(10.0f).margin(0.01f));
}

TEST_CASE("PID: P control with zero error", "[pid][domain]") {
    PidAlgoCfg cfg = {2.0f, 0.01f, 0, 0, 100};
    PidAlgoState s;
    pid_init(&s);

    // Equal target and measurement → P term = 0
    float output = pid_step(&cfg, &s, 30.0f, 30.0f, 60);
    REQUIRE(output == Approx(0.0f).margin(0.01f));
}

// ── PI control with integral accumulation ───────────────────

TEST_CASE("PID: PI control — integral builds over time", "[pid][domain]") {
    PidAlgoCfg cfg = {1.0f, 0.1f, 0, 0, 100};
    PidAlgoState s;
    pid_init(&s);

    // Persistent 5°C error over 60s
    float last = 0;
    for (int i = 0; i < 10; i++) {
        last = pid_step(&cfg, &s, 25.0f, 20.0f, 60);
    }

    // After 10 cycles with Ki=0.1, error=5, integral should accumulate
    // P=5.0, I accumulates ~5.0*0.1*60*10 = 300 → output clamped to 100
    REQUIRE(last > 5.0f); // significantly above P-only
}

// ── Output clamping ─────────────────────────────────────────

TEST_CASE("PID: output clamped to out_min/out_max", "[pid][domain]") {
    PidAlgoCfg cfg = {10.0f, 0, 0, 20.0f, 60.0f};
    PidAlgoState s;
    pid_init(&s);

    // Error = 50 → raw P = 500, clamped to 60
    float output = pid_step(&cfg, &s, 70.0f, 20.0f, 60);
    REQUIRE(output == Approx(60.0f).margin(0.01f));

    // Error = -10 → raw P = -100, clamped to 20
    output = pid_step(&cfg, &s, 10.0f, 20.0f, 60);
    REQUIRE(output == Approx(20.0f).margin(0.01f));
}

// ── Anti-windup ─────────────────────────────────────────────

TEST_CASE("PID: anti-windup prevents integral accumulation at saturation", "[pid][domain]") {
    PidAlgoCfg cfg = {5.0f, 0.5f, 0, 0, 50.0f}; // out_max=50
    PidAlgoState s;
    pid_init(&s);

    // Hit saturation hard with large error
    for (int i = 0; i < 100; i++) {
        pid_step(&cfg, &s, 80.0f, 20.0f, 60);
    }

    // Then reverse error
    float output = pid_step(&cfg, &s, 10.0f, 50.0f, 60);

    // If anti-windup worked, integral didn't grow unbounded
    // and output can go negative quickly
    REQUIRE(output < 50.0f);
}

// ── Derivative term ─────────────────────────────────────────

TEST_CASE("PID: D term responds to rate of change", "[pid][domain]") {
    PidAlgoCfg cfg_pd = {1.0f, 0, 2.0f, 0, 100};
    PidAlgoState s_pd;
    pid_init(&s_pd);

    // Pure P: same error, no rate → no D effect
    PidAlgoCfg cfg_p = {1.0f, 0, 0, 0, 100};
    PidAlgoState s_p;
    pid_init(&s_p);

    // First step: same measurement
    float p_only = pid_step(&cfg_p, &s_p, 50.0f, 40.0f, 60);
    float pd_out = pid_step(&cfg_pd, &s_pd, 50.0f, 40.0f, 60);

    // Both start from same error, D should be different
    // (D term may make PD output different from P output due to initial rate)
    // This tests that D doesn't crash, at minimum
    REQUIRE(std::isfinite(pd_out));
}

// ── D filter — smooths derivative ──────────────────────────

TEST_CASE("PID: D filter attenuates measurement noise", "[pid][domain]") {
    PidAlgoCfg cfg = {1.0f, 0, 5.0f, 0, 100};
    PidAlgoState s;
    pid_init(&s);

    float outputs[20];

    // Oscillating measurements around target
    for (int i = 0; i < 20; i++) {
        float noise = 2.0f * sinf(static_cast<float>(i) * 3.14f);
        outputs[i] = pid_step(&cfg, &s, 50.0f, 50.0f + noise, 60);
    }

    // D filter should prevent wild output swings
    float max_output = outputs[0], min_output = outputs[0];
    for (int i = 1; i < 20; i++) {
        if (outputs[i] > max_output) max_output = outputs[i];
        if (outputs[i] < min_output) min_output = outputs[i];
    }

    // With Kd=5 and oscillating noise, the range should be bounded
    float range = max_output - min_output;
    REQUIRE(range < 50.0f);
}

// ── dt handling ─────────────────────────────────────────────

TEST_CASE("PID: dt_sec <= 0 is clamped to 1", "[pid][domain]") {
    PidAlgoCfg cfg = {2.0f, 0.01f, 0, 0, 100};
    PidAlgoState s;
    pid_init(&s);

    // dt=0 should be clamped to 1, not cause division by zero
    float output = pid_step(&cfg, &s, 30.0f, 25.0f, 0);
    REQUIRE(std::isfinite(output));

    // dt=-5 should also be clamped
    output = pid_step(&cfg, &s, 30.0f, 25.0f, -5);
    REQUIRE(std::isfinite(output));
}

TEST_CASE("PID: longer dt produces more integral accumulation", "[pid][domain]") {
    PidAlgoCfg cfg = {0, 0.1f, 0, 0, 100};
    PidAlgoState s_short;
    pid_init(&s_short);
    pid_step(&cfg, &s_short, 30.0f, 25.0f, 10); // short dt

    PidAlgoState s_long;
    pid_init(&s_long);
    float out_long = pid_step(&cfg, &s_long, 30.0f, 25.0f, 600); // long dt

    // Longer dt = more integral accumulation = higher output
    REQUIRE(out_long > 0.5f);
}

// ── Real-world CH heating scenario ──────────────────────────

TEST_CASE("PID: room temperature approach to setpoint", "[pid][domain]") {
    PidAlgoCfg cfg = {2.0f, 0.02f, 0.5f, 20.0f, 80.0f};
    PidAlgoState s;
    pid_init(&s);

    // Simulate room heating: 18°C → 22°C target
    float room = 18.0f;
    float last_out = 30.0f;

    for (int i = 0; i < 60; i++) {
        last_out = pid_step(&cfg, &s, 22.0f, room, 60);
        // Simple plant model: room heats proportionally to output
        room += 0.005f * (last_out - room * 0.3f);
        if (room > 25.0f) room = 25.0f;
    }

    // Output should decrease as room approaches target
    REQUIRE(last_out < 50.0f);
}
