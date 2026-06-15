/// Tests for PidQualityAssessor (application/services/pid_quality_assessor.h)
/// Covers: quality scoring for overshoot, steady-state, stability, cycling, clamp.

#include <catch2/catch_test_macros.hpp>
#include "application/services/pid_quality_assessor.h"
#include "fakes/fake_heating_state_store.h"

static constexpr int POLLS_PER_MINUTE = 55;
static constexpr int MINUTES = 10;

/// Run one "minute" of poll calls (55 ticks) with current FakeHeatingStateStore values.
static void run_minute(PidQualityAssessor& assessor)
{
    for (int i = 0; i < POLLS_PER_MINUTE; i++) {
        assessor.poll();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Ideal tracking — all scores high
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: ideal tracking gives high scores", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    for (int m = 0; m < MINUTES; m++) {
        state.pid_room_temp_     = 22.0f;
        state.pid_target_room_   = 22.0f;
        state.set_flame(m % 2 == 0);      // alternate → 50% ratio for high cycling score
        state.set_dhw_active(false);
        state.pid_cycle_locked_  = false;
        state.pid_output_        = 50.0f;
        state.set_ch_sp_min(25.0f);
        state.set_ch_sp_max(75.0f);
        run_minute(assessor);
    }

    CHECK(assessor.scores().composite    >= 90.0f);
    CHECK(assessor.scores().overshoot    >= 95.0f);
    CHECK(assessor.scores().steady_state >= 95.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Overshoot — overshoot score drops
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: overshoot reduces overshoot score", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    // Overshoot profile: room_temp drifts up past target (22.0) and overshoots
    float room_temps[MINUTES] = {
        21.0f, 21.5f, 22.0f, 22.3f,   // ramp up + small overshoot
        23.0f, 23.2f, 23.0f,          // large overshoot (1.2°C)
        22.2f, 22.0f, 22.0f           // settle back
    };

    for (int m = 0; m < MINUTES; m++) {
        state.pid_room_temp_     = room_temps[m];
        state.pid_target_room_   = 22.0f;
        state.set_flame(true);
        state.set_dhw_active(false);
        state.pid_cycle_locked_  = false;
        state.pid_output_        = 50.0f;
        state.set_ch_sp_min(25.0f);
        state.set_ch_sp_max(75.0f);
        run_minute(assessor);
    }

    // 1.2°C overshoot should heavily penalise the overshoot score
    CHECK(assessor.scores().overshoot < 60.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Oscillations — stability score drops
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: oscillations reduce stability score", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    // Room temp oscillates around target (22.0) — 9 zero-crossings in 10 minutes
    float room_temps[MINUTES] = {
        21.5f, 22.5f, 21.5f, 22.5f, 21.5f,
        22.5f, 21.5f, 22.5f, 21.5f, 22.5f
    };

    for (int m = 0; m < MINUTES; m++) {
        state.pid_room_temp_     = room_temps[m];
        state.pid_target_room_   = 22.0f;
        state.set_flame(true);
        state.set_dhw_active(false);
        state.pid_cycle_locked_  = false;
        state.pid_output_        = 50.0f;
        state.set_ch_sp_min(25.0f);
        state.set_ch_sp_max(75.0f);
        run_minute(assessor);
    }

    // Strong oscillations → stability score should be low
    CHECK(assessor.scores().stability < 40.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Clamp — clamp score drops
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: clamp reduces clamp score", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    for (int m = 0; m < MINUTES; m++) {
        state.pid_room_temp_     = 22.0f;
        state.pid_target_room_   = 22.0f;
        state.set_flame(true);
        state.set_dhw_active(false);
        state.pid_cycle_locked_  = false;
        state.pid_output_        = 25.0f;   // output == sp_min → clamped
        state.set_ch_sp_min(25.0f);
        state.set_ch_sp_max(75.0f);
        run_minute(assessor);
    }

    // All samples clamped → clamp score should be far below 100
    CHECK(assessor.scores().clamp < 100.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Empty buffer — scores are zero
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: empty buffer gives zero scores", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    assessor.reset();

    CHECK(assessor.scores().composite  == 0.0f);
    CHECK(assessor.scores().overshoot  == 0.0f);
    CHECK(assessor.ring_count()        == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: Buffer does not overflow
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("PidQualityAssessor: ring buffer does not overflow", "[pid_quality]")
{
    FakeHeatingStateStore state;
    PidQualityAssessor assessor(state);

    // Set stable values once (performance optimisation — 82500 polls)
    state.pid_room_temp_     = 22.0f;
    state.pid_target_room_   = 22.0f;
    state.set_flame(false);
    state.set_dhw_active(false);
    state.pid_cycle_locked_  = false;
    state.pid_output_        = 50.0f;
    state.set_ch_sp_min(25.0f);
    state.set_ch_sp_max(75.0f);

    // 1500 "minutes" × 55 polls = 82500 poll calls (more than RING_SIZE=1440)
    for (int m = 0; m < 1500; m++) {
        run_minute(assessor);
    }

    // Ring should be full but not overrun
    CHECK(assessor.ring_count() == 1440);
    CHECK(assessor.ring_head()  < 1440);
}
