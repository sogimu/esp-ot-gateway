#include "application/ports/driven/iheating_stats_store.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Make private members accessible for daily tracking unit tests
#define private public
#include "application/services/gas_flow_estimator.h"
#undef private
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// GasFlowService — weekly daily-consumption tracking (RAM only)
// ═══════════════════════════════════════════════════════════════

namespace {

struct FakeHeatingStatsStore : IHeatingStatsStore {
    void save_stats(const IHeatingStateStore&, uint32_t, float, const void*, const void*, const void*, const void*) override {}
    bool load_stats(uint32_t&, float&, void*, void*, void*, void*) override { return false; }
    void save_total_uptime(uint32_t) override {}
    bool load_total_uptime(uint32_t&) override { return false; }
    void save_integral(float) override {}
    void save_meter(const IHeatingStateStore&, const void*) override {}
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
};

// Fixed reference: 2025-01-15 00:00:00 UTC (epoch day 20103)
constexpr uint64_t DAY0_US = 1736899200ULL * 1000000ULL;

struct Harness {
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    GasFlowService svc{state, time, hss};

    Harness(uint64_t start_us = DAY0_US) {
        time.set_us(start_us);
        // Boiler model
        state.set_p_max(24.0f);
        state.set_gas_calorific(9.5f);
    }

    void boiler_on() {
        state.set_modulation(50.0f);
        state.set_return_temp(45.0f);
        state.set_flame(true);
    }

    void boiler_off() {
        state.set_flame(false);
    }

    void tick(uint32_t ms = 10000) {
        time.advance_ms(ms);
        svc.execute();
    }

    // First execute() only sets last_update_ms_ (no work). Second one
    // initializes daily tracking. Subsequent ticks accumulate.
    void warmup() {
        tick();           // last_update_ms_ init
        tick();           // daily tracking init (today_epoch_day_ set)
    }
};

int64_t epoch_day_of(uint64_t us) {
    return static_cast<int64_t>(us / 1000000ULL / 86400);
}

} // namespace

TEST_CASE("GasDaily: init sets today_epoch_day_, clears accumulator", "[gas_daily]")
{
    Harness h;
    h.tick();  // last_update_ms_ init only
    REQUIRE(h.svc.today_epoch_day_ < 0);

    h.tick();  // initializes daily tracking
    REQUIRE(h.svc.today_epoch_day_ == epoch_day_of(h.time.now_us()));
    REQUIRE(h.svc.daily_accumulator_ == 0);
}

TEST_CASE("GasDaily: accumulates flow into daily_accumulator_", "[gas_daily]")
{
    Harness h;
    h.warmup();
    REQUIRE(h.svc.daily_accumulator_ == 0);

    h.boiler_on();
    h.tick();  // accumulates 10s of flow
    h.tick();  // another 10s
    REQUIRE(h.svc.daily_accumulator_ > 0);
    // integral tracks the same physical flow
    REQUIRE(h.svc.daily_accumulator_ == Approx(h.svc.integral_m3()));
}

TEST_CASE("GasDaily: correction (set_integral(0)) does NOT reset daily accumulator", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    h.tick();
    h.tick();

    float acc_before = h.svc.daily_accumulator_;
    REQUIRE(acc_before > 0);

    // Simulate meter correction: integral is zeroed, daily accumulator survives
    h.svc.set_integral(0);
    REQUIRE(h.svc.integral_m3() == 0);
    REQUIRE(h.svc.daily_accumulator_ == Approx(acc_before));

    // Gas keeps flowing after correction
    h.tick();
    REQUIRE(h.svc.daily_accumulator_ > acc_before);
}

TEST_CASE("GasDaily: day change archives yesterday and resets accumulator", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();

    // Accumulate ~1 minute of flow on day 0
    for (int i = 0; i < 6; i++) h.tick();
    float day0_consumption = h.svc.daily_accumulator_;
    REQUIRE(day0_consumption > 0);

    // Jump to the next day (midnight) and tick again
    uint64_t day1_start_us = h.time.now_us() + 86400ULL * 1000000ULL;
    h.time.set_us(day1_start_us);
    h.tick();

    REQUIRE(h.svc.daily_count_ == 1);
    REQUIRE(h.svc.daily_[0].epoch_day == epoch_day_of(DAY0_US));
    REQUIRE(h.svc.daily_[0].m3 == Approx(day0_consumption));
    REQUIRE(h.svc.daily_accumulator_ == 0);
    REQUIRE(h.svc.today_epoch_day_ == epoch_day_of(day1_start_us));
}

TEST_CASE("GasDaily: get_daily_view returns completed days + today last", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();

    // Day 0: some consumption, then archive on jump to day 1
    for (int i = 0; i < 6; i++) h.tick();
    float day0_m3 = h.svc.daily_accumulator_;
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();  // archives day 0, resets accumulator

    // Day 1: consumption, then archive on jump to day 2
    for (int i = 0; i < 3; i++) h.tick();
    float day1_m3 = h.svc.daily_accumulator_;
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();  // archives day 1, resets accumulator

    // Today (day 2): running consumption
    for (int i = 0; i < 2; i++) h.tick();
    float today_m3 = h.svc.daily_accumulator_;

    GasFlowService::DailyView out[8];
    int n = h.svc.get_daily_view(out, 8);
    REQUIRE(n == 3);  // day0, day1 (archived), today (running)
    REQUIRE(out[0].epoch_day == epoch_day_of(DAY0_US));
    REQUIRE(out[0].m3 == Approx(day0_m3));
    REQUIRE(out[1].epoch_day == epoch_day_of(DAY0_US) + 1);
    REQUIRE(out[1].m3 == Approx(day1_m3));
    REQUIRE(out[2].epoch_day == h.svc.today_epoch_day_);  // today last
    REQUIRE(out[2].m3 == Approx(today_m3));
}

TEST_CASE("GasDaily: ring buffer keeps last 7 completed days", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();

    // Run 10 day transitions with 1 tick per day (accumulating gas each day)
    for (int day = 0; day < 10; day++) {
        h.tick();  // accumulate a little on current day
        h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
        h.tick();  // triggers day archive
    }

    REQUIRE(h.svc.daily_count_ == GasFlowService::DAILY_SLOTS - 1);

    GasFlowService::DailyView out[8];
    int n = h.svc.get_daily_view(out, 8);
    REQUIRE(n == GasFlowService::DAILY_SLOTS);  // 7 completed + today
    // Oldest archived day must be 7 days before today
    REQUIRE(out[0].epoch_day == h.svc.today_epoch_day_ - 7);
    REQUIRE(out[n - 1].epoch_day == h.svc.today_epoch_day_);
}

TEST_CASE("GasDaily: reset() clears daily tracking", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();
    REQUIRE(h.svc.daily_count_ == 1);

    h.svc.reset();
    REQUIRE(h.svc.daily_count_ == 0);
    REQUIRE(h.svc.daily_head_ == 0);
    REQUIRE(h.svc.today_epoch_day_ < 0);
    REQUIRE(h.svc.daily_accumulator_ == 0);
}

TEST_CASE("GasDaily: unsynced wall clock does not initialize daily tracking", "[gas_daily]")
{
    Harness h;
    h.time.set_synced(false);
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();

    REQUIRE(h.svc.today_epoch_day_ < 0);
    REQUIRE(h.svc.daily_count_ == 0);
    // integral still accumulates normally without wall clock
    REQUIRE(h.svc.integral_m3() > 0);
}
