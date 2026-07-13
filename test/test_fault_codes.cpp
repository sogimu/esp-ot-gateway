/// Tests for fault code formatting and rendering.
/// Covers: FaultCodes::oem_fault_text, FaultCodes::asf_flags_text,
///         WebPresenterAdapter JSON output with asf_text field.
///
/// ASF flags are standard OpenTherm — same for all boilers.
/// OEM code is manufacturer-specific — we never guess its meaning,
/// only show the raw number. oem_fault_text() is for journal logging only,
/// NOT exposed in JSON/MQTT/web UI.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <string>

#include "domain/value_objects/fault_codes.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "fakes/fake_heating_state_store.h"

// ── FaultCodes::oem_fault_text (journal-only, not in JSON) ──────

TEST_CASE("oem_fault_text: code 0 is 'no error'", "[fault][domain]") {
    REQUIRE(std::string(FaultCodes::oem_fault_text(0)) == "нет ошибки");
}

TEST_CASE("oem_fault_text: non-zero code includes numeric value", "[fault][domain]") {
    REQUIRE(std::string(FaultCodes::oem_fault_text(118)) == "код 118");
}

TEST_CASE("oem_fault_text: code 1", "[fault][domain]") {
    REQUIRE(std::string(FaultCodes::oem_fault_text(1)) == "код 1");
}

TEST_CASE("oem_fault_text: code 255", "[fault][domain]") {
    REQUIRE(std::string(FaultCodes::oem_fault_text(255)) == "код 255");
}

TEST_CASE("oem_fault_text: codes 0..10 all return non-empty", "[fault][domain]") {
    for (uint8_t code = 0; code <= 10; code++) {
        const char* text = FaultCodes::oem_fault_text(code);
        REQUIRE(text != nullptr);
        REQUIRE(std::strlen(text) > 0);
        if (code == 0) {
            REQUIRE(std::string(text) == "нет ошибки");
        } else {
            REQUIRE(std::string(text).find("код ") != std::string::npos);
        }
    }
}

// ── FaultCodes::asf_flags_text (standard OpenTherm, universal) ──

TEST_CASE("asf_flags_text: zero flags is 'no flags'", "[fault][domain]") {
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
    // bits 0 (сервис), 2 (низкое давление), 5 (перегрев)
    FaultCodes::asf_flags_text(0x25, buf, sizeof(buf));
    std::string s(buf);
    REQUIRE(s.find("сервис") != std::string::npos);
    REQUIRE(s.find("низкое давление воды") != std::string::npos);
    REQUIRE(s.find("перегрев") != std::string::npos);
    REQUIRE(s.find(", ") != std::string::npos);
}

TEST_CASE("asf_flags_text: all standard bits", "[fault][domain]") {
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

// ── WebPresenterAdapter: asf_text in status JSON (universal) ────
// OEM code is present as raw number (oem_fault) but NOT expanded
// into a human-readable description — that would be manufacturer-specific.

TEST_CASE("render_status: no fault, no ASF flags", "[fault][integration]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.fault_ = false;
    state.oem_fault_ = 0;
    state.asf_flags_ = 0;

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[4096];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"fault\":0") != std::string::npos);
    REQUIRE(json.find("\"oem_fault\":0") != std::string::npos);
    REQUIRE(json.find("\"asf_text\":\"нет флагов\"") != std::string::npos);
    // fault_text must NOT be in JSON — OEM expansion is manufacturer-specific
    REQUIRE(json.find("fault_text") == std::string::npos);
}

TEST_CASE("render_status: fault active, ASF flags decoded", "[fault][integration]") {
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

    // Raw numbers always present
    REQUIRE(json.find("\"fault\":1") != std::string::npos);
    REQUIRE(json.find("\"oem_fault\":118") != std::string::npos);
    // ASF decoded (standard OpenTherm, universal)
    REQUIRE(json.find("\"asf_text\":\"низкое давление воды\"") != std::string::npos);
    // No manufacturer-specific OEM expansion
    REQUIRE(json.find("fault_text") == std::string::npos);
}

TEST_CASE("render_status: service reminder without hard fault", "[fault][integration]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.fault_ = false;
    state.oem_fault_ = 0;
    state.asf_flags_ = 0x01;  // service request

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[4096];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"fault\":0") != std::string::npos);
    REQUIRE(json.find("\"asf_text\":\"сервис\"") != std::string::npos);
    REQUIRE(json.find("fault_text") == std::string::npos);
}

TEST_CASE("render_status: no state set — valid empty JSON", "[fault][integration]") {
    WebPresenterAdapter presenter;
    char buf[64];
    int len = presenter.render_status(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);
    REQUIRE(json == "{}");
}
