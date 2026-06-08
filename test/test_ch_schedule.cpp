/// Tests for CH_Schedule value object (domain/value_objects/ch_schedule.h)
/// Covers: validation, hourly access, boundary conditions.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/value_objects/ch_schedule.h"
#include <cstring>

using Catch::Approx;

TEST_CASE("CH_Schedule: default is disabled with zeros", "[schedule][domain]") {
    CH_Schedule s;
    REQUIRE(s.enabled == false);
    REQUIRE(s.get_for_hour(0) == Approx(0.0f));
    REQUIRE(s.get_for_hour(12) == Approx(0.0f));
}

TEST_CASE("CH_Schedule: valid temperatures pass validation", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = true;
    for (int i = 0; i < 24; i++) s.temps[i] = 30.0f;
    REQUIRE(s.is_valid());
}

TEST_CASE("CH_Schedule: temperature below 20 fails validation", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = true;
    for (int i = 0; i < 24; i++) s.temps[i] = 30.0f;
    s.temps[5] = 19.9f;
    REQUIRE(!s.is_valid());
}

TEST_CASE("CH_Schedule: temperature above 80 fails validation", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = true;
    for (int i = 0; i < 24; i++) s.temps[i] = 30.0f;
    s.temps[10] = 80.1f;
    REQUIRE(!s.is_valid());
}

TEST_CASE("CH_Schedule: zero temps are technically invalid (< 20)", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = false;
    // Default all-zeros is invalid, but enabled=false compensates
    REQUIRE(!s.is_valid());
}

TEST_CASE("CH_Schedule: get_for_hour retrieves correct hour", "[schedule][domain]") {
    CH_Schedule s;
    for (int i = 0; i < 24; i++) s.temps[i] = 20.0f + static_cast<float>(i);

    REQUIRE(s.get_for_hour(0) == Approx(20.0f));
    REQUIRE(s.get_for_hour(12) == Approx(32.0f));
    REQUIRE(s.get_for_hour(23) == Approx(43.0f));
}

TEST_CASE("CH_Schedule: get_for_hour clamps out-of-range", "[schedule][domain]") {
    CH_Schedule s;
    for (int i = 0; i < 24; i++) s.temps[i] = 25.0f;

    // Negative hour → default 30.0
    REQUIRE(s.get_for_hour(-1) == Approx(30.0f));
    // Beyond 23 → default 30.0
    REQUIRE(s.get_for_hour(24) == Approx(30.0f));
    REQUIRE(s.get_for_hour(100) == Approx(30.0f));
}

TEST_CASE("CH_Schedule: boundary temps 20 and 80 are valid", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = true;
    for (int i = 0; i < 24; i++) s.temps[i] = 20.0f; // minimum valid
    REQUIRE(s.is_valid());

    for (int i = 0; i < 24; i++) s.temps[i] = 80.0f; // maximum valid
    REQUIRE(s.is_valid());
}

TEST_CASE("CH_Schedule: typical daily schedule", "[schedule][domain]") {
    CH_Schedule s;
    s.enabled = true;

    // Night: 21°C (hours 0-5)
    for (int h = 0; h < 6; h++) s.temps[h] = 21.0f;
    // Morning: 23°C (hours 6-8)
    for (int h = 6; h < 9; h++) s.temps[h] = 23.0f;
    // Day: 20°C (hours 9-16)
    for (int h = 9; h < 17; h++) s.temps[h] = 20.0f;
    // Evening: 24°C (hours 17-22)
    for (int h = 17; h < 23; h++) s.temps[h] = 24.0f;
    // Night: 21°C (hour 23)
    s.temps[23] = 21.0f;

    REQUIRE(s.is_valid());
    REQUIRE(s.get_for_hour(3) == Approx(21.0f));
    REQUIRE(s.get_for_hour(7) == Approx(23.0f));
    REQUIRE(s.get_for_hour(12) == Approx(20.0f));
    REQUIRE(s.get_for_hour(19) == Approx(24.0f));
}
