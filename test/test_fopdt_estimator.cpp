/// Tests for FopdtEstimator — FOPDT parameter estimation from heating events.
///
/// Covers: full cycle (IDLE→HEAT_UP→SETTLING→STEADY), DHW interruption,
/// overshoot, short cycle, ring-buffer capping, and cooling tau collection.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "application/services/fopdt_estimator.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

// ── Helpers ─────────────────────────────────────────────────────────────────

/// Run one poll cycle: configure state, call poll(), advance time.
/// Constructor now initializes prev_ms_ from time_.monotonic_ms(),
/// so the first poll() has a valid baseline — no priming needed.
#define POLL_STEP()   do { est.execute(); time.advance_ms(1000); } while(0)

// ── Test 1: Full cycle IDLE → HEAT_UP → SETTLING → STEADY ──────────────────

TEST_CASE("FOPDT full cycle", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;

    // Step 0: initial, flame off
    state.pid_room_temp_ = 20.0f;
    state.flame_         = false;
    POLL_STEP();

    // Step 1: flame on → IDLE→HEAT_UP
    state.flame_ = true;
    POLL_STEP();

    // Step 2: barely rising, below MIN_RISE (0.1)
    state.pid_room_temp_ = 20.05f;
    POLL_STEP();

    // Step 3: rise exceeds MIN_RISE → first_rise_ms → dead_time ≈ 2 s
    state.pid_room_temp_ = 20.15f;
    POLL_STEP();

    // Steps 4–10: steady heating
    for (float r : {20.3f, 20.5f, 20.7f, 20.9f, 21.1f, 21.3f, 21.5f}) {
        state.pid_room_temp_ = r;
        POLL_STEP();
    }

    // Step 11: enters band [target-0.3, target+0.3] → SETTLING
    state.pid_room_temp_ = 21.7f;
    POLL_STEP();

    // Steps 12–16: settling in band → finalize after 5 stable counts
    for (float r : {21.8f, 21.9f, 22.0f, 22.1f, 22.1f}) {
        state.pid_room_temp_ = r;
        POLL_STEP();
    }

    CHECK(est.event_count() >= 1);
    CHECK(est.dead_time_sec() > 0.0f);
    CHECK(est.dead_time_sec() < 10.0f);
    CHECK(est.gain() > 0.5f);
    CHECK(est.gain() < 2.0f);
    CHECK(est.time_constant_heat_sec() > 0.0f);
    CHECK(est.time_constant_heat_sec() < 36000.0f);
}

// ── Test 2: DHW interrupts an active event ──────────────────────────────────

TEST_CASE("FOPDT DHW interrupt prevents finalization", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;
    state.pid_room_temp_  = 20.0f;

    // Step 0: flame on → HEAT_UP
    state.flame_ = true;
    POLL_STEP();

    // Step 1: temperature rising
    state.pid_room_temp_ = 20.3f;
    POLL_STEP();

    // Step 2: DHW becomes active → IDLE reset
    state.pid_room_temp_ = 20.5f;
    state.dhw_active_    = true;
    POLL_STEP();

    // Step 3: still DHW
    state.pid_room_temp_ = 20.7f;
    POLL_STEP();

    // Step 4: DHW ends, flame still on
    state.dhw_active_ = false;
    state.pid_room_temp_ = 21.0f;
    POLL_STEP();

    CHECK(est.event_count() == 0);
}

// ── Test 3: Overshoot during heating ────────────────────────────────────────

TEST_CASE("FOPDT overshoot produces gain > 1.0", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;
    state.pid_room_temp_  = 20.0f;

    // Step 0: flame on → HEAT_UP
    state.flame_ = true;
    POLL_STEP();

    // Steps 1–4: rapid heating
    for (float r : {20.5f, 21.0f, 21.5f, 21.9f}) {
        state.pid_room_temp_ = r;
        POLL_STEP();
    }

    // Step 5: enters band → SETTLING
    state.pid_room_temp_ = 22.0f;
    POLL_STEP();

    // Step 6: overshoot beyond band
    state.pid_room_temp_ = 22.5f;
    POLL_STEP();

    // Step 7: flame off, room still above band
    state.pid_room_temp_ = 22.7f;
    state.flame_         = false;
    POLL_STEP();

    // Steps 8–14: cooling back into band, accumulate stable counts
    for (float r : {22.5f, 22.3f, 22.1f, 22.0f, 22.0f, 22.0f, 22.0f}) {
        state.pid_room_temp_ = r;
        POLL_STEP();
    }

    CHECK(est.event_count() >= 1);
    CHECK(est.gain() > 1.0f);
}

// ── Test 4: Short cycle — flame off before reaching band ────────────────────

TEST_CASE("FOPDT short cycle does not finalize", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;
    state.pid_room_temp_  = 20.0f;

    // Step 0: flame on → HEAT_UP
    state.flame_ = true;
    POLL_STEP();

    // Step 1: some rise
    state.pid_room_temp_ = 20.3f;
    POLL_STEP();

    // Step 2: flame off before reaching band → back to IDLE
    state.pid_room_temp_ = 20.6f;
    state.flame_         = false;
    POLL_STEP();

    CHECK(est.event_count() == 0);
}

// ── Test 5: Multiple events fill then cap the ring buffer ───────────────────

TEST_CASE("FOPDT ring buffer caps at EVENT_RING_SIZE", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;

    constexpr int N_CYCLES = 12;

    for (int c = 0; c < N_CYCLES; c++) {
        // ── Between cycles: flame off, room cools ──────────
        state.flame_         = false;
        state.pid_room_temp_ = 20.0f;
        POLL_STEP();                          // prev_flame_ becomes false

        // ── Start new cycle ────────────────────────────────
        state.flame_ = true;
        POLL_STEP();                          // IDLE → HEAT_UP

        // Quick heating (large steps)
        state.pid_room_temp_ = 20.5f;
        POLL_STEP();
        state.pid_room_temp_ = 21.0f;
        POLL_STEP();
        state.pid_room_temp_ = 21.5f;
        POLL_STEP();
        state.pid_room_temp_ = 21.8f;          // enters band → SETTLING
        POLL_STEP();

        // Settle for 5 counts
        state.pid_room_temp_ = 22.0f;
        for (int s = 0; s < 4; s++) {
            POLL_STEP();
        }
        // After 5 stable counts finalize_event is called.
        // (settle_stable_count_ = 1 on entry + 4 more = 5)
    }

    CHECK(est.event_count() == 10);           // EVENT_RING_SIZE
}

// ── Test 6: Cooling phase produces tau_cool > 0 in next event ──────────────

TEST_CASE("FOPDT cooling phase yields tau_cool", "[fopdt]")
{
    FakeHeatingStateStore state;
    FakeTimeSource       time;
    FopdtEstimator       est(state, time);

    state.pid_target_room_ = 22.0f;
    state.outside_temp_    = 0.0f;            // fixed outside temp

    // ── First heating cycle ─────────────────────────────────
    state.pid_room_temp_ = 20.0f;
    state.flame_         = false;
    POLL_STEP();

    state.flame_ = true;
    POLL_STEP();                              // HEAT_UP

    for (float r : {20.5f, 21.0f, 21.5f})
        { state.pid_room_temp_ = r; POLL_STEP(); }

    state.pid_room_temp_ = 21.8f;              // enters band → SETTLING
    POLL_STEP();

    state.pid_room_temp_ = 22.0f;
    for (int s = 0; s < 4; s++) POLL_STEP();  // finalize (5 stable)

    // ── Cooling phase (flame off, temp dropping) ────────────
    state.flame_ = false;
    for (float r : {22.0f, 21.8f, 21.6f, 21.4f, 21.2f,
                    21.0f, 20.8f, 20.6f, 20.4f})
    {
        state.pid_room_temp_ = r;
        POLL_STEP();                          // accumulates cool_rate
    }

    // ── Second heating cycle ────────────────────────────────
    state.flame_ = true;
    POLL_STEP();                              // HEAT_UP

    for (float r : {20.6f, 21.0f, 21.5f})
        { state.pid_room_temp_ = r; POLL_STEP(); }

    state.pid_room_temp_ = 21.8f;              // enters band → SETTLING
    POLL_STEP();

    state.pid_room_temp_ = 22.0f;
    for (int s = 0; s < 4; s++) POLL_STEP();  // finalize

    // The second event should carry tau_cool from measured cooling data.
    bool found_cool = false;
    for (int i = 0; i < est.event_count(); i++) {
        if (est.events()[i].tau_cool_sec > 0.0f) {
            found_cool = true;
            break;
        }
    }
    CHECK(found_cool);
}
