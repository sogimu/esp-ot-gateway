/// Tests for ScheduleService (application/services/schedule_service.h)
/// Covers: evaluate with schedule enabled/disabled, hour boundaries, validation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/schedule_service.h"

using Catch::Approx;

TEST_CASE("ScheduleService: returns no setpoint when schedule disabled", "[sched][app]") {
    float temps[24] = {};
    for (int i = 0; i < 24; i++) temps[i] = 30.0f;

    auto r = ScheduleService::evaluate(false, temps, 12);
    REQUIRE(r.has_setpoint == false);
}

TEST_CASE("ScheduleService: returns setpoint for current hour", "[sched][app]") {
    float temps[24] = {};
    temps[8] = 50.0f;  // 08:00 = 50°C

    auto r = ScheduleService::evaluate(true, temps, 8);
    REQUIRE(r.has_setpoint == true);
    REQUIRE(r.setpoint == Approx(50.0f));
}

TEST_CASE("ScheduleService: out of range hour returns no setpoint", "[sched][app]") {
    float temps[24] = {};
    for (int i = 0; i < 24; i++) temps[i] = 30.0f;

    auto r = ScheduleService::evaluate(true, temps, -1);
    REQUIRE(r.has_setpoint == false);

    r = ScheduleService::evaluate(true, temps, 24);
    REQUIRE(r.has_setpoint == false);
}

TEST_CASE("ScheduleService: invalid setpoint in schedule returns no setpoint", "[sched][app]") {
    float temps[24] = {};
    for (int i = 0; i < 24; i++) temps[i] = 30.0f;
    temps[10] = 10.0f; // invalid (< 20°C)

    auto r = ScheduleService::evaluate(true, temps, 10);
    REQUIRE(r.has_setpoint == false);
}

TEST_CASE("ScheduleService: hour 0 (midnight) works", "[sched][app]") {
    float temps[24] = {};
    temps[0] = 21.0f;

    auto r = ScheduleService::evaluate(true, temps, 0);
    REQUIRE(r.has_setpoint == true);
    REQUIRE(r.setpoint == Approx(21.0f));
}

TEST_CASE("ScheduleService: hour 23 (last hour) works", "[sched][app]") {
    float temps[24] = {};
    temps[23] = 22.0f;

    auto r = ScheduleService::evaluate(true, temps, 23);
    REQUIRE(r.has_setpoint == true);
    REQUIRE(r.setpoint == Approx(22.0f));
}

// ── is_valid_setpoint ──────────────────────────────────────

TEST_CASE("ScheduleService: valid setpoints", "[sched][app]") {
    REQUIRE(ScheduleService::is_valid_setpoint(20.0f));
    REQUIRE(ScheduleService::is_valid_setpoint(50.0f));
    REQUIRE(ScheduleService::is_valid_setpoint(80.0f));
}

TEST_CASE("ScheduleService: invalid setpoints", "[sched][app]") {
    REQUIRE(!ScheduleService::is_valid_setpoint(19.9f));
    REQUIRE(!ScheduleService::is_valid_setpoint(80.1f));
    REQUIRE(!ScheduleService::is_valid_setpoint(0.0f));
    REQUIRE(!ScheduleService::is_valid_setpoint(-5.0f));
    REQUIRE(!ScheduleService::is_valid_setpoint(100.0f));
}
