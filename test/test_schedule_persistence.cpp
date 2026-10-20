/// Tests for CH_Schedule persistence contract (state-store round-trip + blob size)
/// and for the `hour` field in WebPresenterAdapter::render_schedule().
///
/// BoilerNvsStore itself depends on ESP-IDF NVS and is not testable on host;
/// these tests pin the ABI and round-trip contract that the NVS blob relies on.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/value_objects/ch_schedule.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"
#include <string>
#include <cstring>

using Catch::Approx;

TEST_CASE("CH_Schedule blob size is stable for NVS", "[schedule][persistence]") {
    // bool (4) + float[24] (96) = 100 bytes. Changing this breaks the persisted
    // "ch_sched" NVS blob and requires a migration.
    REQUIRE(sizeof(CH_Schedule) == 100);
}

TEST_CASE("CH_Schedule round-trips through state store (enabled + 24 temps)", "[schedule][persistence]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    CH_Schedule in;
    in.enabled = true;
    for (int h = 0; h < 24; h++) in.temps[h] = 20.0f + static_cast<float>(h);

    state.set_schedule(&in);

    CH_Schedule out;
    state.get_schedule(&out);

    REQUIRE(out.enabled == true);
    for (int h = 0; h < 24; h++) {
        REQUIRE(out.temps[h] == Approx(in.temps[h]));
    }
}

TEST_CASE("CH_Schedule round-trips disabled state", "[schedule][persistence]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    CH_Schedule in;
    in.enabled = false;
    for (int h = 0; h < 24; h++) in.temps[h] = 30.0f;

    state.set_schedule(&in);

    CH_Schedule out;
    state.get_schedule(&out);

    REQUIRE(out.enabled == false);
    REQUIRE(out.temps[0] == Approx(30.0f));
}

TEST_CASE("render_schedule: no time source -> hour=-1", "[schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    CH_Schedule sched;
    sched.enabled = true;
    for (int h = 0; h < 24; h++) sched.temps[h] = 40.0f;
    state.set_schedule(&sched);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);

    char buf[512];
    int len = presenter.render_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":-1") != std::string::npos);
    REQUIRE(json.find("\"enabled\":1") != std::string::npos);
    REQUIRE(json.find("\"temps\":[") != std::string::npos);
}

TEST_CASE("render_schedule: synced time -> hour=0 at epoch", "[schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    CH_Schedule sched;
    sched.enabled = true;
    for (int h = 0; h < 24; h++) sched.temps[h] = 40.0f;
    state.set_schedule(&sched);

    FakeTimeSource time;
    time.set_synced(true);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_time(&time);

    char buf[512];
    int len = presenter.render_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":0") != std::string::npos);
}

TEST_CASE("render_schedule: unsynced time -> hour=-1", "[schedule][render]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();

    FakeTimeSource time;
    time.set_synced(false);

    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_time(&time);

    char buf[512];
    int len = presenter.render_schedule(buf, sizeof(buf));
    REQUIRE(len > 0);
    std::string json(buf, len);

    REQUIRE(json.find("\"hour\":-1") != std::string::npos);
}
