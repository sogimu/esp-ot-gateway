/// Tests for setpoint clamping against boiler-reported bounds (ID 49/48),
/// centralized in SystemConfigInteractor.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/system_config_interactor.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/iboiler_config_store.h"
#include "domain/value_objects/ch_schedule.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_boiler_hardware.h"
#include "fakes/fake_time_source.h"
#include <cstdio>
#include <cstdarg>

using Catch::Approx;

struct FakeLogger : public ILogger {
    void event(ILogger::Category, const char* fmt, ...) override {
        va_list args; va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args);
        event_count_++;
    }
    char last_msg_[128] = {};
    int event_count_ = 0;
};

struct FakeBoilerConfig : IBoilerConfigStore {
    void load_boiler_config(IHeatingStateStore&) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}
};

struct BoundsFixture {
    FakeHeatingStateStore state;
    FakeBoilerHardware boiler;
    FakeConfigurationStore config;
    FakeLogger log;
    FakeTimeSource time;
    FakeBoilerConfig boiler_cfg;
    SystemConfigInteractor sys{state, boiler, config, boiler_cfg, log, time};
};

TEST_CASE("Setpoint bounds: CH clamped to boiler bounds", "[bounds][app]") {
    BoundsFixture f;
    f.state.set_ch_sp_min(25.0f);
    f.state.set_ch_sp_max(60.0f);

    f.sys.set_ch_setpoint(20.0f);
    REQUIRE(f.state.get_ch_setpoint() == Approx(25.0f));

    f.sys.set_ch_setpoint(70.0f);
    REQUIRE(f.state.get_ch_setpoint() == Approx(60.0f));

    f.sys.set_ch_setpoint(40.0f);
    REQUIRE(f.state.get_ch_setpoint() == Approx(40.0f));
}

TEST_CASE("Setpoint bounds: DHW clamped to boiler bounds", "[bounds][app]") {
    BoundsFixture f;
    f.state.set_dhw_sp_min(35.0f);
    f.state.set_dhw_sp_max(60.0f);

    f.sys.set_dhw_setpoint(10.0f);
    REQUIRE(f.state.get_dhw_setpoint() == Approx(35.0f));

    f.sys.set_dhw_setpoint(70.0f);
    REQUIRE(f.state.get_dhw_setpoint() == Approx(60.0f));
}

TEST_CASE("Setpoint bounds: fallback range before boiler bounds are read", "[bounds][app]") {
    BoundsFixture f;  // defaults: ch_sp_min_=0, ch_sp_max_=80 (unread)

    f.sys.set_ch_setpoint(20.0f);
    REQUIRE(f.state.get_ch_setpoint() == Approx(25.0f));  // fallback lo = 25

    f.sys.set_dhw_setpoint(70.0f);
    REQUIRE(f.state.get_dhw_setpoint() == Approx(60.0f)); // fallback hi = 60
}

TEST_CASE("Setpoint bounds: schedule entries clamped", "[bounds][app]") {
    BoundsFixture f;
    f.state.set_ch_sp_min(25.0f);
    f.state.set_ch_sp_max(60.0f);

    CH_Schedule sched;
    sched.enabled = true;
    for (int h = 0; h < 24; h++) sched.temps[h] = 30.0f;
    sched.temps[0] = 10.0f;   // below min → 25
    sched.temps[1] = 90.0f;   // above max → 60

    f.sys.set_schedule(sched);

    CH_Schedule out;
    f.state.get_schedule(&out);
    REQUIRE(out.temps[0] == Approx(25.0f));
    REQUIRE(out.temps[1] == Approx(60.0f));
    REQUIRE(out.temps[2] == Approx(30.0f));
}
