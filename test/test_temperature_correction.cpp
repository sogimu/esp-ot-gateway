/// Tests for user temperature correction: TemperatureSourceAdapter (raw + offset)
/// and SystemConfigInteractor::set_temperature_offsets (clamping, partial update).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/system_config_interactor.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/iboiler_config_store.h"
#include "infrastructure/driven/temperature_source_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "infrastructure/driving/json_helpers.h"
#include "domain/value_objects/ch_schedule.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_boiler_hardware.h"
#include "fakes/fake_time_source.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

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

struct TempCorrFixture {
    FakeHeatingStateStore state;
    FakeBoilerHardware boiler;
    FakeConfigurationStore config;
    FakeLogger log;
    FakeTimeSource time;
    FakeBoilerConfig boiler_cfg;
    SystemConfigInteractor sys{state, boiler, config, boiler_cfg, log, time};
};

TEST_CASE("TemperatureSourceAdapter: corrected = raw + offset", "[tempcorr]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.set_t1_temp(21.0f);
    state.set_t1_offset(-1.5f);
    state.set_outside_temp(5.0f);
    state.set_outside_offset(2.0f);

    TemperatureSourceAdapter temps(state);
    REQUIRE(temps.t1() == Approx(19.5f));
    REQUIRE(temps.outside() == Approx(7.0f));
}

TEST_CASE("TemperatureSourceAdapter: zero offset returns raw", "[tempcorr]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.set_t2_temp(18.25f);

    TemperatureSourceAdapter temps(state);
    REQUIRE(temps.t2() == Approx(18.25f));
}

TEST_CASE("TemperatureSourceAdapter: CH/DHW temperatures are not corrected", "[tempcorr]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.set_ch_temp(45.0f);
    state.set_return_temp(35.0f);
    state.set_dhw_temp(55.0f);

    TemperatureSourceAdapter temps(state);
    REQUIRE(temps.ch_flow() == Approx(45.0f));
    REQUIRE(temps.ch_return() == Approx(35.0f));
    REQUIRE(temps.dhw() == Approx(55.0f));
}

TEST_CASE("Temperature offsets: clamped to ±10", "[tempcorr]") {
    TempCorrFixture f;

    f.sys.set_temperature_offsets(25.0f, -25.0f, 5.0f);

    REQUIRE(f.state.get_t1_offset() == Approx(10.0f));
    REQUIRE(f.state.get_t2_offset() == Approx(-10.0f));
    REQUIRE(f.state.get_outside_offset() == Approx(5.0f));
}

TEST_CASE("Temperature offsets: rounded to 0.1 °C", "[tempcorr]") {
    TempCorrFixture f;

    f.sys.set_temperature_offsets(1.234f, -3.789f, 0.05f);

    REQUIRE(f.state.get_t1_offset() == Approx(1.2f));
    REQUIRE(f.state.get_t2_offset() == Approx(-3.8f));
    REQUIRE(f.state.get_outside_offset() == Approx(0.1f));
}

TEST_CASE("HTTP body offsets are parsed and applied to T1", "[tempcorr]") {
    TempCorrFixture f;
    f.state.set_t1_temp(21.0f);

    // What the web panel sends when only T1 is edited (others empty → null).
    const char* body = "{\"t1_offset\":2.5,\"t2_offset\":null,\"outside_offset\":null}";
    float t1o = json_get_float(body, "\"t1_offset\"");
    float t2o = json_get_float(body, "\"t2_offset\"");
    float oo  = json_get_float(body, "\"outside_offset\"");
    if (t1o > -1e37f || t2o > -1e37f || oo > -1e37f)
        f.sys.set_temperature_offsets(t1o, t2o, oo);

    REQUIRE(f.state.get_t1_offset() == Approx(2.5f));
    TemperatureSourceAdapter temps(f.state);
    REQUIRE(temps.t1() == Approx(23.5f));
}

TEST_CASE("render_status reports corrected T1", "[tempcorr]") {
    FakeHeatingStateStore state;
    state.reset_to_defaults();
    state.set_t1_temp(21.0f);
    state.set_t1_offset(2.5f);

    TemperatureSourceAdapter temps(state);
    WebPresenterAdapter presenter;
    presenter.set_state(&state);
    presenter.set_temps(&temps);

    char buf[2048];
    presenter.render_status(buf, sizeof(buf));
    REQUIRE(std::strstr(buf, "\"t1_temp\":23.5") != nullptr);
}

TEST_CASE("Temperature offsets: absent fields left unchanged", "[tempcorr]") {
    TempCorrFixture f;
    f.state.set_t1_offset(2.0f);
    f.state.set_t2_offset(-1.0f);

    const float NA = -1e38f;
    f.sys.set_temperature_offsets(NA, NA, 1.5f);

    REQUIRE(f.state.get_t1_offset() == Approx(2.0f));
    REQUIRE(f.state.get_t2_offset() == Approx(-1.0f));
    REQUIRE(f.state.get_outside_offset() == Approx(1.5f));
}
