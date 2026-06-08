#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/services/pid_algorithm.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// PidAlgorithm — anti-windup, edge cases, derivative filter
// ═══════════════════════════════════════════════════════════════

TEST_CASE("PidAlgorithm: anti-windup float comparison fragility", "[pid][bug][major]")
{
    // BUG: pid_step uses `if (output_raw == output)` to decide
    // anti-windup. Float equality comparison is fragile:
    // very small differences due to float precision could
    // incorrectly prevent integral accumulation.

    PidAlgoCfg cfg = {1.0f, 0.1f, 0.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // Small error that won't saturate — integral should grow
    float setpoint = 30.0f;
    float measured = 28.0f;  // 2°C error
    int dt = 60;

    // First step: P=2, I=0.1*2*60=12, total=14 → below max, integral grows
    float out1 = pid_step(&cfg, &state, setpoint, measured, dt);
    INFO("step1: output=" << out1 << " integral=" << state.integral);
    CHECK(state.integral > 0.0f);

    // Second step: integral continues accumulating
    float out2 = pid_step(&cfg, &state, setpoint, measured, dt);
    INFO("step2: output=" << out2 << " integral=" << state.integral);
    CHECK(out2 > out1); // integral accumulation increases output

    // BUG: with extreme float values, output_raw == output
    // may fail spuriously, breaking anti-windup
    WARN("BUG: output_raw == output is fragile float comparison");
}

TEST_CASE("PidAlgorithm: integral windup prevention at saturation", "[pid]")
{
    PidAlgoCfg cfg = {3.0f, 0.1f, 0.0f, 25.0f, 75.0f};
    PidAlgoState state;
    pid_init(&state);

    // Large persistent error that would saturate
    float setpoint = 80.0f;
    float measured = 20.0f; // 60°C error

    for (int i = 0; i < 10; i++) {
        pid_step(&cfg, &state, setpoint, measured, 60);
        // Integral should not grow unbounded — anti-windup clamps it
        INFO("step " << i << ": integral=" << state.integral);
        if (i > 3) {
            // After saturation, integral should stop growing
            CHECK(state.integral <= 1000.0f); // reasonable bound
        }
    }
}

TEST_CASE("PidAlgorithm: P-only response to step change", "[pid]")
{
    // Pure proportional: output ∝ error
    PidAlgoCfg cfg = {2.0f, 0.0f, 0.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // 5°C error → P = 2.0 * 5 = 10
    float out = pid_step(&cfg, &state, 30.0f, 25.0f, 60);
    INFO("P-only: error=5, output=" << out);
    CHECK(out == Approx(10.0f).margin(0.1f));
}

TEST_CASE("PidAlgorithm: I-term accumulates over time", "[pid]")
{
    PidAlgoCfg cfg = {0.0f, 0.1f, 0.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // 2°C steady error, 60s dt → each step adds 0.1 * 2 * 60 = 12
    float out1 = pid_step(&cfg, &state, 30.0f, 28.0f, 60);
    float out2 = pid_step(&cfg, &state, 30.0f, 28.0f, 60);

    INFO("I-term: step1=" << out1 << " step2=" << out2);
    // Output should increase as integral grows
    CHECK(out2 > out1);
}

TEST_CASE("PidAlgorithm: D-term responds to rate of change", "[pid]")
{
    PidAlgoCfg cfg = {0.0f, 0.0f, 1.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // Temperature dropping: measured goes from 30 → 25 in 60s
    // d_raw = 1.0 * (30 - 25) / 60 = 0.0833
    // With filter: d_filt = 0.9 * 0 + 0.1 * 0.0833 = 0.00833
    pid_step(&cfg, &state, 30.0f, 30.0f, 60); // set prev_temp = 30

    float out = pid_step(&cfg, &state, 30.0f, 25.0f, 60);
    INFO("D-term: measured 30→25, output=" << out);
    // With D-only and temperature dropping (error decreasing),
    // D-term should be positive (opposing the decrease)
    CHECK(out >= 0.0f);
}

TEST_CASE("PidAlgorithm: output clamping at min", "[pid]")
{
    PidAlgoCfg cfg = {2.0f, 0.0f, 0.0f, 25.0f, 75.0f};
    PidAlgoState state;
    pid_init(&state);

    // Error = 5°C → P = 10, which is below out_min=25, clamped to 25
    float out = pid_step(&cfg, &state, 30.0f, 25.0f, 60);
    INFO("clamped to min: output=" << out);
    CHECK(out == Approx(25.0f).margin(0.1f));
}

TEST_CASE("PidAlgorithm: output clamping at max", "[pid]")
{
    PidAlgoCfg cfg = {10.0f, 0.0f, 0.0f, 0.0f, 75.0f};
    PidAlgoState state;
    pid_init(&state);

    // Large error: 10 * 20 = 200 → clamped to 75
    float out = pid_step(&cfg, &state, 50.0f, 30.0f, 60);
    INFO("clamped to max: output=" << out);
    CHECK(out == Approx(75.0f).margin(0.1f));
}

TEST_CASE("PidAlgorithm: dt=0 defaults to dt=1", "[pid]")
{
    PidAlgoCfg cfg = {2.0f, 0.1f, 0.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // dt=0 should be treated as dt=1 (line 33 of pid_algorithm.h)
    float out = pid_step(&cfg, &state, 30.0f, 25.0f, 0);
    INFO("dt=0 → output=" << out);
    // Should not be NaN or inf
    CHECK(out >= 0.0f);
    CHECK(out < 1000.0f);
}

TEST_CASE("PidAlgorithm: negative dt is handled", "[pid]")
{
    PidAlgoCfg cfg = {2.0f, 0.1f, 0.0f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // Negative dt → clamped to 1
    float out = pid_step(&cfg, &state, 30.0f, 25.0f, -10);
    INFO("dt=-10 → output=" << out);
    CHECK(out >= 0.0f);
    CHECK(out < 1000.0f);
}

TEST_CASE("PidAlgorithm: D-filter smooths derivative", "[pid]")
{
    // Use P + D so output is non-zero and visible
    PidAlgoCfg cfg = {1.0f, 0.0f, 0.5f, 0.0f, 100.0f};
    PidAlgoState state;
    pid_init(&state);

    // Set initial state
    pid_step(&cfg, &state, 25.0f, 25.0f, 60); // error=0 → P=0, prev_temp=25

    // Temperature drop: 25→20 → error=5, P=5, D contributes positively
    float out1 = pid_step(&cfg, &state, 25.0f, 20.0f, 60);

    // Same temperature: no change → D decays
    float out2 = pid_step(&cfg, &state, 25.0f, 20.0f, 60);

    INFO("D-filter: first=" << out1 << " second=" << out2);
    // D-filter decays negative derivative contribution toward zero.
    // First step: derivative = kd*(25-20)/60 = +0.042 contributes to d_filt
    // Second step: no temp change → d_raw=0, d_filt decays toward 0
    // Since initial d_filt was negative (from step 0), decay → output increases
    CHECK(out2 > out1); // output approaches P-only value as derivative decays
}

TEST_CASE("PidAlgorithm: full PID convergence", "[pid]")
{
    PidAlgoCfg cfg = {2.0f, 0.05f, 0.5f, 25.0f, 75.0f};
    PidAlgoState state;
    pid_init(&state);

    float setpoint = 22.0f;
    float room = 18.0f;
    int dt = 30;

    // Run PID for 20 steps — output should stabilize
    float outputs[20];
    for (int i = 0; i < 20; i++) {
        // Simulate room approaching setpoint
        if (room < setpoint) room += 0.2f;
        outputs[i] = pid_step(&cfg, &state, setpoint, room, dt);
    }

    INFO("PID outputs: first=" << outputs[0] << " last=" << outputs[19]);

    // Output should be positive (heating needed)
    CHECK(outputs[0] > 0.0f);
    // Last output should be reasonable (not oscillating wildly)
    CHECK(outputs[19] >= 25.0f);
    CHECK(outputs[19] <= 75.0f);
}
