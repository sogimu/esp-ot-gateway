/// Tests for timezone persistence via SystemConfigInteractor.
/// Regression: json_get_int returned -1 for missing JSON key, and
/// the guard v > -100 passed, calling set_timezone(-1) whenever
/// /api/control was POSTed without a tz_offset field (e.g. every
/// "Применить" click on the Heating tab).

#include <catch2/catch_test_macros.hpp>
#include "application/use_cases/system_config_interactor.h"
#include "application/ports/driven/ilogger.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_boiler_hardware.h"
#include "fakes/fake_time_source.h"
#include <cstdio>
#include <cstdarg>

struct FakeLogger : public ILogger {
    void event(ILogger::Category, const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args);
        event_count_++;
    }
    char last_msg_[128] = {};
    int event_count_ = 0;
};

TEST_CASE("Timezone: set_timezone(7) persists +7 to state and config store", "[tz]") {
    FakeHeatingStateStore state;
    FakeBoilerHardware boiler;
    FakeConfigurationStore config;
    FakeLogger log;
    FakeTimeSource time;
    SystemConfigInteractor sys(state, boiler, config, log, time);

    sys.set_timezone(7);

    REQUIRE(state.get_tz_offset() == 7);
    REQUIRE(config.save_config_called_ > 0);
}

TEST_CASE("Timezone: unrelated config change does not overwrite timezone", "[tz]") {
    FakeHeatingStateStore state;
    FakeBoilerHardware boiler;
    FakeConfigurationStore config;
    FakeLogger log;
    FakeTimeSource time;
    SystemConfigInteractor sys(state, boiler, config, log, time);

    sys.set_timezone(7);
    REQUIRE(state.get_tz_offset() == 7);

    // Simulate applyControl POST that changes heating params
    // but does NOT include tz_offset
    sys.set_ch_mode(0);
    sys.set_ch_setpoint(65);

    // Timezone must still be 7
    REQUIRE(state.get_tz_offset() == 7);
}

TEST_CASE("Timezone: valid range -12..+14 is accepted", "[tz]") {
    FakeHeatingStateStore state;
    FakeBoilerHardware boiler;
    FakeConfigurationStore config;
    FakeLogger log;
    FakeTimeSource time;
    SystemConfigInteractor sys(state, boiler, config, log, time);

    sys.set_timezone(7);
    REQUIRE(state.get_tz_offset() == 7);

    sys.set_timezone(-12);
    REQUIRE(state.get_tz_offset() == -12);

    sys.set_timezone(14);
    REQUIRE(state.get_tz_offset() == 14);
}
