/// Tests for the DHW on/off schedule: pure gate function, state-store
/// round-trip, and WebPresenterAdapter::render_dhw_schedule().

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/services/dhw_schedule_service.h"
#include "application/services/dhw_hysteresis_service.h"
#include "domain/value_objects/ch_schedule.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"
#include <string>
#include <cstring>

using Catch::Approx;

// ── DhwScheduleService::is_allowed ────────────────────────

TEST_CASE("DHW schedule: allowed hour returns true", "[dhw][schedule]") {
    bool on[24] = {};
    for (int i = 0; i < 24; i++) on[i] = true;
    REQUIRE(DhwScheduleService::is_allowed(true, on, 8));
}

TEST_CASE("DHW schedule: forbidden hour returns false", "[dhw][schedule]") {
    bool on[24] = {};
    for (int i = 0; i < 24; i++) on[i] = true;
    on[3] = false;
    REQUIRE(!DhwScheduleService::is_allowed(true, on, 3));
}

TEST_CASE("DHW schedule: disabled schedule is fail-open", "[dhw][schedule]") {
    bool on[24] = {};  // all forbidden
    REQUIRE(DhwScheduleService::is_allowed(false, on, 3));
}

TEST_CASE("DHW schedule: invalid hour is fail-open (allowed)", "[dhw][schedule]") {
    bool on[24] = {};  // all forbidden
    REQUIRE(DhwScheduleService::is_allowed(true, on, -1));
    REQUIRE(DhwScheduleService::is_allowed(true, on, 24));
}

TEST_CASE("DHW schedule: hour 0 and 23 boundaries", "[dhw][schedule]") {
    bool on[24] = {};
    on[0] = true; on[23] = false;
    REQUIRE(DhwScheduleService::is_allowed(true, on, 0));
    REQUIRE(!DhwScheduleService::is_allowed(true, on, 23));
}

// ── Combined gate: master switch + schedule (interactor expression) ──

TEST_CASE("DHW schedule: forbidden hour blocks heating below setpoint", "[dhw][schedule]") {
    bool on[24] = {};
    for (int i = 0; i < 24; i++) on[i] = true;
    on[3] = false;
    bool dhw_enable = true;
    bool dhw_allowed = dhw_enable && DhwScheduleService::is_allowed(true, on, 3);
    REQUIRE(!dhw_allowed);
    REQUIRE(!DHWHysteresisService::should_heat(40.0f, 55.0f, 2.0f, dhw_allowed));
}

TEST_CASE("DHW schedule: master switch off overrides allowed hour", "[dhw][schedule]") {
    bool on[24] = {};
    for (int i = 0; i < 24; i++) on[i] = true;
    bool dhw_enable = false;
    bool dhw_allowed = dhw_enable && DhwScheduleService::is_allowed(true, on, 10);
    REQUIRE(!dhw_allowed);
    REQUIRE(!DHWHysteresisService::should_heat(40.0f, 55.0f, 2.0f, dhw_allowed));
}

// ── Persistence (state-store round-trip + blob size) ──────

TEST_CASE("DHW_Schedule blob size is stable for NVS", "[dhw][schedule][persistence]") {
    // bool (1) + bool[24] (24) = 25 bytes. Changing this breaks the persisted
    // "dhw_sched" NVS blob and requires a migration.
    REQUIRE(sizeof(DHW_Schedule) == 25);
}

TEST_CASE("DHW_Schedule round-trips through state store (enabled + 24 on)", "[dhw][schedule][persistence]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    DHW_Schedule in;
    in.enabled = true;
    for (int h = 0; h < 24; h++) in.on[h] = (h % 2 == 0);

    state.set_dhw_schedule(&in);

    DHW_Schedule out;
    state.get_dhw_schedule(&out);

    REQUIRE(out.enabled == true);
    for (int h = 0; h < 24; h++) {
        REQUIRE(out.on[h] == in.on[h]);
    }
}

TEST_CASE("DHW_Schedule round-trips disabled state", "[dhw][schedule][persistence]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    DHW_Schedule in;
    in.enabled = false;
    for (int h = 0; h < 24; h++) in.on[h] = true;

    state.set_dhw_schedule(&in);

    DHW_Schedule out;
    state.get_dhw_schedule(&out);

    REQUIRE(out.enabled == false);
    REQUIRE(out.on[0] == true);
}

// ── render_dhw_schedule ───────────────────────────────────

TEST_CASE("render_dhw_schedule: no time source -> hour=-1", "[dhw][schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    DHW_Schedule sched;
    sched.enabled = true;
    for (int h = 0; h < 24; h++) sched.on[h] = true;
    state.set_dhw_schedule(&sched);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[512];
    int len = presenter.render_dhw_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":-1") != std::string::npos);
    REQUIRE(json.find("\"enabled\":1") != std::string::npos);
    REQUIRE(json.find("\"on\":[") != std::string::npos);
}

TEST_CASE("render_dhw_schedule: synced time -> hour=0 at epoch", "[dhw][schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    DHW_Schedule sched;
    sched.enabled = true;
    for (int h = 0; h < 24; h++) sched.on[h] = true;
    state.set_dhw_schedule(&sched);

    FakeTimeSource time;
    time.set_synced(true);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_time(&time);

    char buf[512];
    int len = presenter.render_dhw_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":0") != std::string::npos);
}

TEST_CASE("render_dhw_schedule: unsynced time -> hour=-1", "[dhw][schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    FakeTimeSource time;
    time.set_synced(false);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_time(&time);

    char buf[512];
    int len = presenter.render_dhw_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":-1") != std::string::npos);
}
