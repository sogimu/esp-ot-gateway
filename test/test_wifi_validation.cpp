/// Host-compilable unit tests for WiFi credential validation.
/// Tests domain/value_objects/wifi_validation.cpp — pure C++ logic, no ESP-IDF needed.

#include <catch2/catch_test_macros.hpp>
#include "domain/value_objects/wifi_validation.h"

TEST_CASE("validate_ssid — empty string", "[wifi][validation]") {
    auto r = validate_ssid("");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ssid — valid short", "[wifi][validation]") {
    auto r = validate_ssid("ab");
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_ssid — valid with space", "[wifi][validation]") {
    auto r = validate_ssid("My WiFi");
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_ssid — too long (>32)", "[wifi][validation]") {
    auto r = validate_ssid("123456789012345678901234567890123");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ssid — exactly 32 chars", "[wifi][validation]") {
    char ssid[33] = {};
    for (int i = 0; i < 32; i++) ssid[i] = 'a';
    auto r = validate_ssid(ssid);
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_ssid — non-ASCII (Cyrillic)", "[wifi][validation]") {
    auto r = validate_ssid("МояСеть");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ssid — null pointer", "[wifi][validation]") {
    auto r = validate_ssid(nullptr);
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_wifi_password — too short (<8)", "[wifi][validation]") {
    auto r = validate_wifi_password("1234567");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_wifi_password — exactly 8", "[wifi][validation]") {
    auto r = validate_wifi_password("12345678");
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_wifi_password — valid long", "[wifi][validation]") {
    auto r = validate_wifi_password("my_secure_password_123");
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_wifi_password — too long (>63)", "[wifi][validation]") {
    char pwd[65] = {};
    for (int i = 0; i < 64; i++) pwd[i] = 'x';
    auto r = validate_wifi_password(pwd);
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_wifi_password — exactly 63", "[wifi][validation]") {
    char pwd[64] = {};
    for (int i = 0; i < 63; i++) pwd[i] = 'x';
    auto r = validate_wifi_password(pwd);
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_wifi_password — null pointer", "[wifi][validation]") {
    auto r = validate_wifi_password(nullptr);
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ap_password — empty", "[wifi][validation]") {
    auto r = validate_ap_password("");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ap_password — too short", "[wifi][validation]") {
    auto r = validate_ap_password("1234567");
    REQUIRE(r.ok == false);
}

TEST_CASE("validate_ap_password — valid", "[wifi][validation]") {
    auto r = validate_ap_password("secret123");
    REQUIRE(r.ok == true);
}

TEST_CASE("validate_ap_password — null", "[wifi][validation]") {
    auto r = validate_ap_password(nullptr);
    REQUIRE(r.ok == false);
}
