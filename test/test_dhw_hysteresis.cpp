/// Tests for DHW hysteresis service (application/services/dhw_hysteresis_service.h)
/// Covers: should_heat, should_stop, edge cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/dhw_hysteresis_service.h"

using Catch::Approx;

// ── should_heat ─────────────────────────────────────────────

TEST_CASE("DHW hysteresis: should_heat returns true when below threshold", "[dhw][app]") {
    // DHW at 50°C, setpoint 55°C, hysteresis 2°C
    // Threshold = 55 - 2 = 53°C → 50 < 53 → should heat
    REQUIRE(DHWHysteresisService::should_heat(50.0f, 55.0f, 2.0f, true));
}

TEST_CASE("DHW hysteresis: should_heat returns false when above lower threshold", "[dhw][app]") {
    // DHW at 54°C, setpoint 55°C, hysteresis 2°C
    // Threshold = 55 - 2 = 53°C → 54 > 53 → should NOT heat
    REQUIRE(!DHWHysteresisService::should_heat(54.0f, 55.0f, 2.0f, true));
}

TEST_CASE("DHW hysteresis: should_heat returns false when DHW disabled", "[dhw][app]") {
    // Temperature below threshold, but DHW mode disabled
    REQUIRE(!DHWHysteresisService::should_heat(40.0f, 55.0f, 2.0f, false));
}

TEST_CASE("DHW hysteresis: at exact threshold — no heat", "[dhw][app]") {
    // DHW exactly at hysteresis threshold
    REQUIRE(!DHWHysteresisService::should_heat(53.0f, 55.0f, 2.0f, true));
}

TEST_CASE("DHW hysteresis: works with default hysteresis of 2°C", "[dhw][app]") {
    // Typical Baxi DHW: setpoint 55, hysteresis 2, start heat at 53
    REQUIRE(DHWHysteresisService::should_heat(52.9f, 55.0f, 2.0f, true));
    REQUIRE(!DHWHysteresisService::should_heat(53.1f, 55.0f, 2.0f, true));
}

// ── should_stop ─────────────────────────────────────────────

TEST_CASE("DHW hysteresis: should_stop returns true at or above setpoint", "[dhw][app]") {
    REQUIRE(DHWHysteresisService::should_stop(55.0f, 55.0f));
    REQUIRE(DHWHysteresisService::should_stop(56.0f, 55.0f));
    REQUIRE(DHWHysteresisService::should_stop(80.0f, 55.0f));
}

TEST_CASE("DHW hysteresis: should_stop returns false below setpoint", "[dhw][app]") {
    REQUIRE(!DHWHysteresisService::should_stop(54.9f, 55.0f));
    REQUIRE(!DHWHysteresisService::should_stop(40.0f, 55.0f));
}

// ── Edge cases ──────────────────────────────────────────────

TEST_CASE("DHW hysteresis: zero hysteresis — stop/start at same point", "[dhw][app]") {
    // With 0 hysteresis, should_heat when strictly below setpoint
    REQUIRE(DHWHysteresisService::should_heat(54.9f, 55.0f, 0.0f, true));
    REQUIRE(!DHWHysteresisService::should_heat(55.0f, 55.0f, 0.0f, true));
}

TEST_CASE("DHW hysteresis: large hysteresis band", "[dhw][app]") {
    // 10°C hysteresis: heat starts at 45°C for 55°C setpoint
    REQUIRE(DHWHysteresisService::should_heat(44.9f, 55.0f, 10.0f, true));
    REQUIRE(!DHWHysteresisService::should_heat(45.1f, 55.0f, 10.0f, true));
}

TEST_CASE("DHW hysteresis: typical Baxi scenario", "[dhw][app]") {
    // Baxi Duo-tec: setpoint 55, hysteresis 2, temp decays from 55 to 52
    // At 55: should stop
    REQUIRE(DHWHysteresisService::should_stop(55.0f, 55.0f));

    // At 53: above hysteresis threshold, still don't heat
    REQUIRE(!DHWHysteresisService::should_heat(53.5f, 55.0f, 2.0f, true));

    // At 52.9: below threshold, start heating
    REQUIRE(DHWHysteresisService::should_heat(52.9f, 55.0f, 2.0f, true));

    // Heat until setpoint reached
    REQUIRE(DHWHysteresisService::should_stop(55.0f, 55.0f));
}
