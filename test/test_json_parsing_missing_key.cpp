/// Tests for json_get_float / json_get_int helpers.
/// Regression: json_get_int returned -1 for missing keys, and a caller
/// guard `v > -100` treated -1 as a valid value. This caused set_timezone(-1)
/// whenever a POST body omitted the tz_offset field (e.g. every "Применить"
/// click on the Heating tab).

#include <catch2/catch_test_macros.hpp>
#include "infrastructure/driving/json_helpers.h"

TEST_CASE("json_get_float: returns sentinel when key is missing", "[json]") {
    const char* body = "{\"ch_mode\":0,\"ch_setpoint\":65}";

    float f = json_get_float(body, "\"tz_offset\"");
    REQUIRE(f < -1e37f);  // sentinel: key not found
}

TEST_CASE("json_get_float: key present returns correct value", "[json]") {
    const char* body = "{\"tz_offset\":7,\"ch_mode\":0}";

    float f = json_get_float(body, "\"tz_offset\"");
    REQUIRE(f == 7.0f);
}

TEST_CASE("json_get_int: returns -1 for missing key", "[json]") {
    // This is the dangerous behaviour: -1 is indistinguishable
    // from a legit value -1. Callers must use json_get_float + sentinel guard.
    const char* body = "{\"ch_mode\":0}";

    int v = json_get_int(body, "\"tz_offset\"");
    REQUIRE(v == -1);
}

TEST_CASE("json_get_float: applyControl-like body does NOT trigger tz_offset", "[json]") {
    // This is the exact JSON sent by applyControl() on the Heating tab.
    // It must NOT be misinterpreted as containing tz_offset.
    const char* body =
        "{\"ch_enable\":1,\"ch_mode\":0,\"ch_setpoint\":65,"
        "\"pid_kp\":2,\"pid_ki\":0.01,\"pid_kd\":0,"
        "\"pid_dt_sec\":60,\"pid_room_sensor\":0,"
        "\"pid_target_room\":22,\"pid_cycle_lockout\":300,"
        "\"pid_hysteresis\":0.5}";

    float f = json_get_float(body, "\"tz_offset\"");
    REQUIRE(f < -1e37f);  // NOT found → must not call set_timezone(-1)
}

TEST_CASE("json_get_float: value -1 is correctly parsed as -1", "[json]") {
    // Distinguishing a legit -1 value from "key not found" (-1e38f sentinel).
    const char* body = "{\"tz_offset\":-1}";

    float f = json_get_float(body, "\"tz_offset\"");
    REQUIRE(f == -1.0f);
    REQUIRE(f > -1e37f);  // not the sentinel
}

TEST_CASE("json_get_float: negative values parse correctly", "[json]") {
    const char* body = "{\"tz_offset\":-5}";

    float f = json_get_float(body, "\"tz_offset\"");
    REQUIRE(f == -5.0f);
    REQUIRE(f > -1e37f);
}

TEST_CASE("json_get_int: trailing zero not dropped (lockout regression)", "[json][regression]")
{
    // Bug: trailing '0' in values like 120, 300 was dropped → 12, 30
    const char* body = "{\"pid_cycle_lockout\":120}";
    int v = json_get_int(body, "\"pid_cycle_lockout\"");
    REQUIRE(v == 120);

    const char* body2 = "{\"pid_cycle_lockout\":300}";
    int v2 = json_get_int(body2, "\"pid_cycle_lockout\"");
    REQUIRE(v2 == 300);

    const char* body3 = "{\"pid_cycle_lockout\":99}";
    int v3 = json_get_int(body3, "\"pid_cycle_lockout\"");
    REQUIRE(v3 == 99);

    // Full body like from applyControl
    const char* full = "{\"ch_enable\":1,\"ch_mode\":3,\"pid_cycle_lockout\":120,\"pid_hysteresis\":0.6}";
    int v4 = json_get_int(full, "\"pid_cycle_lockout\"");
    REQUIRE(v4 == 120);
}
