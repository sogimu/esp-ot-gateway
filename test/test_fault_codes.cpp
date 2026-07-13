/// Tests for ASF flag decoding and JSON rendering.
/// ASF flags are standard OpenTherm — same for all boilers.
/// OEM fault codes are manufacturer-specific and intentionally NOT decoded.

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "domain/value_objects/fault_codes.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "fakes/fake_heating_state_store.h"

// ── FaultCodes::asf_flags_text (standard OpenTherm, universal) ──

TEST_CASE("asf_flags_text: zero flags", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "нет флагов");
}

TEST_CASE("asf_flags_text: low water pressure (bit 2)", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x04, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "низкое давление воды");
}

TEST_CASE("asf_flags_text: service request (bit 0)", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x01, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "сервис");
}

TEST_CASE("asf_flags_text: lockout (bit 1)", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x02, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "блокировка");
}

TEST_CASE("asf_flags_text: over-temperature (bit 5)", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x20, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "перегрев");
}

TEST_CASE("asf_flags_text: multiple flags — comma-separated", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x25, buf, sizeof(buf));  // bits 0, 2, 5
    std::string s(buf);
    REQUIRE(s.find("сервис") != std::string::npos);
    REQUIRE(s.find("низкое давление воды") != std::string::npos);
    REQUIRE(s.find("перегрев") != std::string::npos);
    REQUIRE(s.find(", ") != std::string::npos);
}

TEST_CASE("asf_flags_text: all standard bits fit in buffer", "[fault][domain]") {
    char buf[256];
    FaultCodes::asf_flags_text(0x3F, buf, sizeof(buf));  // bits 0-5
    std::string s(buf);
    REQUIRE(s.find("сервис") != std::string::npos);
    REQUIRE(s.find("блокировка") != std::string::npos);
    REQUIRE(s.find("низкое давление воды") != std::string::npos);
    REQUIRE(s.find("ошибка газа/пламени") != std::string::npos);
    REQUIRE(s.find("ошибка давления воздуха") != std::string::npos);
    REQUIRE(s.find("перегрев") != std::string::npos);
}

// ── WebPresenterAdapter: asf_text in status JSON ────────────────

TEST_CASE("render_status: no fault, no ASF flags", "[fault][integration]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.fault_ = false;
    state.asf_flags_ = 0;

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[4096];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"fault\":0") != std::string::npos);
    REQUIRE(json.find("\"asf_text\":\"нет флагов\"") != std::string::npos);
    // OEM code not expanded — manufacturer-specific
    REQUIRE(json.find("fault_text") == std::string::npos);
}

TEST_CASE("render_status: fault with ASF flags decoded", "[fault][integration]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.fault_ = true;
    state.oem_fault_ = 118;
    state.asf_flags_ = 0x04;  // bit 2: low water pressure

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[4096];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"fault\":1") != std::string::npos);
    REQUIRE(json.find("\"oem_fault\":118") != std::string::npos);
    REQUIRE(json.find("\"asf_text\":\"низкое давление воды\"") != std::string::npos);
    REQUIRE(json.find("fault_text") == std::string::npos);
}

TEST_CASE("render_status: service reminder without hard fault", "[fault][integration]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.fault_ = false;
    state.asf_flags_ = 0x01;  // service request

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[4096];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"fault\":0") != std::string::npos);
    REQUIRE(json.find("\"asf_text\":\"сервис\"") != std::string::npos);
}

TEST_CASE("render_status: no state set — valid empty JSON", "[fault][integration]") {
    WebPresenterAdapter presenter;
    char buf[64];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);
    REQUIRE(json == "{}");
}
