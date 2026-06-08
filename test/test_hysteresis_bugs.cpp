#include <catch2/catch_test_macros.hpp>
#include "application/services/dhw_hysteresis_service.h"

// ═══════════════════════════════════════════════════════════════
// DHWHysteresisService — edge cases and boundary conditions
// ═══════════════════════════════════════════════════════════════

TEST_CASE("DHWHysteresis: should_heat below setpoint minus hysteresis", "[hysteresis]")
{
    // Normal case: temperature is well below setpoint
    SECTION("well below") {
        CHECK(DHWHysteresisService::should_heat(40.0f, 55.0f, 5.0f, true) == true);
    }

    SECTION("near boundary (below)") {
        // temp=49.9, sp=55, hyst=5 → 49.9 < 50.0 → true
        CHECK(DHWHysteresisService::should_heat(49.9f, 55.0f, 5.0f, true) == true);
    }
}

TEST_CASE("DHWHysteresis: should_heat at exact boundary", "[hysteresis][bug][major]")
{
    // BUG: uses strict `<` comparison.
    // At exact equality (temp == setpoint - hysteresis), should_heat returns false.
    // This means the boiler won't start heating at the exact hysteresis boundary.

    // temp=50.0, sp=55, hyst=5 → 50.0 < 50.0 → false
    bool result = DHWHysteresisService::should_heat(50.0f, 55.0f, 5.0f, true);

    INFO("temp==setpoint-hyst: should_heat=" << result);
    INFO("With strict <, heating doesn't start exactly at boundary");

    // Current behavior: false (strict <)
    // Debatable: should this be <= to avoid temperature drift?
    WARN("BUG: should_heat uses strict < — heating may not engage at exact boundary");
    CHECK(result == false); // documents current behavior
}

TEST_CASE("DHWHysteresis: should_heat when disabled", "[hysteresis]")
{
    // Disabled DHW should never heat
    CHECK(DHWHysteresisService::should_heat(10.0f, 55.0f, 5.0f, false) == false);
    CHECK(DHWHysteresisService::should_heat(40.0f, 55.0f, 5.0f, false) == false);
    CHECK(DHWHysteresisService::should_heat(60.0f, 55.0f, 5.0f, false) == false);
}

TEST_CASE("DHWHysteresis: should_heat above setpoint", "[hysteresis]")
{
    // Above setpoint → never heat
    CHECK(DHWHysteresisService::should_heat(55.0f, 55.0f, 5.0f, true) == false);
    CHECK(DHWHysteresisService::should_heat(60.0f, 55.0f, 5.0f, true) == false);
}

TEST_CASE("DHWHysteresis: should_heat within hysteresis band", "[hysteresis]")
{
    // Within hysteresis band: (setpoint - hysteresis) < temp < setpoint
    // 50 < 52 < 55 → should NOT heat (still in band)
    CHECK(DHWHysteresisService::should_heat(52.0f, 55.0f, 5.0f, true) == false);

    // 50 < 54.9 < 55 → should NOT heat
    CHECK(DHWHysteresisService::should_heat(54.9f, 55.0f, 5.0f, true) == false);
}

TEST_CASE("DHWHysteresis: should_stop boundary conditions", "[hysteresis]")
{
    // Stop heating when temp >= setpoint
    SECTION("exactly at setpoint") {
        CHECK(DHWHysteresisService::should_stop(55.0f, 55.0f) == true);
    }

    SECTION("above setpoint") {
        CHECK(DHWHysteresisService::should_stop(56.0f, 55.0f) == true);
    }

    SECTION("below setpoint") {
        CHECK(DHWHysteresisService::should_stop(54.9f, 55.0f) == false);
    }
}

TEST_CASE("DHWHysteresis: should_heat with zero hysteresis", "[hysteresis]")
{
    // Zero hysteresis: any temp below setpoint triggers heating
    CHECK(DHWHysteresisService::should_heat(54.9f, 55.0f, 0.0f, true) == true);
    CHECK(DHWHysteresisService::should_heat(55.0f, 55.0f, 0.0f, true) == false);
}

TEST_CASE("DHWHysteresis: should_heat with large hysteresis", "[hysteresis]")
{
    // Hysteresis = 10°C
    CHECK(DHWHysteresisService::should_heat(44.9f, 55.0f, 10.0f, true) == true);
    CHECK(DHWHysteresisService::should_heat(45.0f, 55.0f, 10.0f, true) == false);
}

TEST_CASE("DHWHysteresis: edge case temperatures", "[hysteresis]")
{
    SECTION("cold water") {
        CHECK(DHWHysteresisService::should_heat(10.0f, 55.0f, 5.0f, true) == true);
    }

    SECTION("hot water above setpoint") {
        CHECK(DHWHysteresisService::should_heat(70.0f, 55.0f, 5.0f, true) == false);
    }

    SECTION("exact boundary setpoint minus hysteresis") {
        // 50.0 == 55.0 - 5.0
        CHECK(DHWHysteresisService::should_heat(50.0f, 55.0f, 5.0f, true) == false);
    }

    SECTION("just below boundary") {
        // 49.99 < 55.0 - 5.0
        CHECK(DHWHysteresisService::should_heat(49.99f, 55.0f, 5.0f, true) == true);
    }
}

TEST_CASE("DHWHysteresis: complete heat-stop cycle", "[hysteresis]")
{
    // Simulate a complete DHW heating cycle
    float sp = 55.0f;
    float hyst = 5.0f;
    bool enabled = true;

    // Start: water at 40°C → should heat
    CHECK(DHWHysteresisService::should_heat(40.0f, sp, hyst, enabled) == true);

    // Heating in progress: 50°C → within band, continues
    CHECK(DHWHysteresisService::should_heat(50.0f, sp, hyst, enabled) == false);
    CHECK(DHWHysteresisService::should_stop(50.0f, sp) == false);

    // Reached setpoint: 55°C → should stop
    CHECK(DHWHysteresisService::should_stop(55.0f, sp) == true);

    // Cooling down: 52°C → within band, should NOT re-heat yet
    CHECK(DHWHysteresisService::should_heat(52.0f, sp, hyst, enabled) == false);

    // Below hysteresis: 49.9°C → should re-heat
    CHECK(DHWHysteresisService::should_heat(49.9f, sp, hyst, enabled) == true);
}
