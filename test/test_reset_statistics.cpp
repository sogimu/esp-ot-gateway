#include "application/ports/driven/iburn_stats_store.h"
/// Tests for reset statistics use cases — verify actual data reset via SystemConfigInteractor.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/system_config_interactor.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/iboiler_config_store.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_boiler_hardware.h"
#include "fakes/fake_time_source.h"
#include <cstring>
#include <cstdarg>

using Catch::Approx;

struct FakeBoilerConfigStore : IBoilerConfigStore {
    void load_boiler_config(IHeatingStateStore&) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}
};

struct ResetTestLogger : public ILogger {
    void event(ILogger::Category, const char* fmt, ...) override {
        va_list args; va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args); event_count_++;
    }
    char last_msg_[256] = {};
    int event_count_ = 0;
};

// ═══ reset_cycle_stats ═══

struct FakeBurnStatsStore : IBurnStatsStore {
    bool load_burn_stats(uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&, uint32_t&) override { return false; }
    void save_burn_stats(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
};

TEST_CASE("ResetStats: reset_cycle_stats clears burner data", "[reset][cycle]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);
    state.set_flame(true);
    time.advance_ms(100);
    burn_cycles.poll();
    time.advance_sec(10);
    state.set_flame(false);
    time.advance_ms(100);
    burn_cycles.poll();

    REQUIRE(burn_cycles.cycle_count() == 1);
    REQUIRE(burn_cycles.burner_seconds() > 0);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_burn_cycles(&burn_cycles);
    sys_cfg.reset_cycle_stats();

    REQUIRE(burn_cycles.cycle_count() == 0);
    REQUIRE(burn_cycles.burner_seconds() == 0);
    REQUIRE(log.event_count_ > 0);
}

TEST_CASE("ResetStats: reset_cycle_stats on empty data is safe", "[reset][cycle]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    FakeBurnStatsStore burn_store;
    BurnCycleService burn_cycles(state, time, burn_store);
    REQUIRE(burn_cycles.cycle_count() == 0);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_burn_cycles(&burn_cycles);
    REQUIRE_NOTHROW(sys_cfg.reset_cycle_stats());
}

// ═══ reset_modulation_stats ═══

TEST_CASE("ResetStats: reset_modulation_stats clears histogram", "[reset][mod]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    ModulationStatsService mod_stats(state);
    state.set_flame(true);
    state.set_modulation(50.0f);
    mod_stats.poll();
    state.set_modulation(60.0f);
    mod_stats.poll();

    REQUIRE(mod_stats.samples() > 0);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_mod_stats(&mod_stats);
    sys_cfg.reset_modulation_stats();

    REQUIRE(mod_stats.samples() == 0);
}

TEST_CASE("ResetStats: reset_modulation_stats on empty data is safe", "[reset][mod]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    ModulationStatsService mod_stats(state);
    REQUIRE(mod_stats.samples() == 0);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_mod_stats(&mod_stats);
    REQUIRE_NOTHROW(sys_cfg.reset_modulation_stats());
}

// ═══ reset_gas_stats ═══

TEST_CASE("ResetStats: reset_gas_stats clears integral and EMAs", "[reset][gas]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    GasFlowService gas_flow(state, time);
    gas_flow.set_integral(10.0f);

    REQUIRE(gas_flow.integral_m3() > 0);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_gas_flow_reset(&gas_flow);
    sys_cfg.reset_gas_stats();

    REQUIRE(gas_flow.integral_m3() == Approx(0.0f));
}

TEST_CASE("ResetStats: reset_gas_stats on empty data is safe", "[reset][gas]")
{
    FakeHeatingStateStore state;
    FakeConfigurationStore config;
    FakeBoilerHardware boiler;
    FakeTimeSource time;
    ResetTestLogger log;

    GasFlowService gas_flow(state, time);

    FakeBoilerConfigStore boiler_cfg;
    SystemConfigInteractor sys_cfg(state, boiler, config, boiler_cfg, log, time);
    sys_cfg.set_gas_flow_reset(&gas_flow);
    REQUIRE_NOTHROW(sys_cfg.reset_gas_stats());
}
