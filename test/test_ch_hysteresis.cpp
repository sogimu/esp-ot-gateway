/// Tests for CH hysteresis service (application/services/ch_hysteresis_service.h)
/// Covers: should_heat, should_stop, edge cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/ch_hysteresis_service.h"

using Catch::Approx;

// ── should_heat ─────────────────────────────────────────────

TEST_CASE("CH hysteresis: should_heat returns true when below threshold", "[ch][app]") {
    // Flow at 24°C, setpoint 26°C, hysteresis 2°C
    // Threshold = 26 - 2 = 24°C → 23.9 < 24 → should heat
    REQUIRE(CHHysteresisService::should_heat(23.9f, 26.0f, 2.0f, true));
}

TEST_CASE("CH hysteresis: should_heat returns false when above lower threshold", "[ch][app]") {
    // Flow at 24.5°C, setpoint 26°C, hysteresis 2°C → above threshold → no heat
    REQUIRE(!CHHysteresisService::should_heat(24.5f, 26.0f, 2.0f, true));
}

TEST_CASE("CH hysteresis: should_heat returns false when CH disabled", "[ch][app]") {
    REQUIRE(!CHHysteresisService::should_heat(20.0f, 26.0f, 2.0f, false));
}

TEST_CASE("CH hysteresis: at exact threshold — no heat", "[ch][app]") {
    // Flow exactly at hysteresis threshold → not below → no heat
    REQUIRE(!CHHysteresisService::should_heat(24.0f, 26.0f, 2.0f, true));
}

// ── should_stop ─────────────────────────────────────────────

TEST_CASE("CH hysteresis: should_stop returns true at or above setpoint", "[ch][app]") {
    REQUIRE(CHHysteresisService::should_stop(26.0f, 26.0f));
    REQUIRE(CHHysteresisService::should_stop(30.0f, 26.0f));
}

TEST_CASE("CH hysteresis: should_stop returns false below setpoint", "[ch][app]") {
    REQUIRE(!CHHysteresisService::should_stop(25.9f, 26.0f));
    REQUIRE(!CHHysteresisService::should_stop(20.0f, 26.0f));
}

// ── Edge cases ──────────────────────────────────────────────

TEST_CASE("CH hysteresis: zero hysteresis — heat when strictly below setpoint", "[ch][app]") {
    REQUIRE(CHHysteresisService::should_heat(25.9f, 26.0f, 0.0f, true));
    REQUIRE(!CHHysteresisService::should_heat(26.0f, 26.0f, 0.0f, true));
}

TEST_CASE("CH hysteresis: large hysteresis band", "[ch][app]") {
    // 5°C hysteresis: heat starts at 21°C for 26°C setpoint
    REQUIRE(CHHysteresisService::should_heat(20.9f, 26.0f, 5.0f, true));
    REQUIRE(!CHHysteresisService::should_heat(21.1f, 26.0f, 5.0f, true));
}

TEST_CASE("CH hysteresis: typical manual-mode scenario", "[ch][app]") {
    // Manual mode, setpoint 26°C, hysteresis 2°C
    // Flow overshoots to 30 (min modulation floor), should stop
    REQUIRE(CHHysteresisService::should_stop(30.0f, 26.0f));

    // Flow decays; above 24 still no heat (dead band)
    REQUIRE(!CHHysteresisService::should_heat(24.5f, 26.0f, 2.0f, true));

    // Flow below 24 → heat
    REQUIRE(CHHysteresisService::should_heat(23.5f, 26.0f, 2.0f, true));

    // Heat until setpoint reached
    REQUIRE(CHHysteresisService::should_stop(26.0f, 26.0f));
}
