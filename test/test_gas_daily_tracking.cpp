#include "application/ports/driven/iheating_stats_store.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Make private members accessible for daily tracking unit tests
#define private public
#include "application/services/gas_flow_estimator.h"
#undef private
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_gas_correction_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// GasFlowService — daily/hourly consumption tracking (RAM + NVS)
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
    FakeGasCorrectionStore gcs;
    GasFlowService svc{state, time, hss, gcs};

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
        tick();           // day/hour tracking init
    }
};

int64_t epoch_day_of(uint64_t us) {
    return static_cast<int64_t>(us / 1000000ULL / 86400);
}

int64_t epoch_hour_of(uint64_t us) {
    return static_cast<int64_t>(us / 1000000ULL / 3600);
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

    GasFlowService::DailyView out[GasFlowService::DAILY_SLOTS];
    int n = h.svc.get_daily_view(out, GasFlowService::DAILY_SLOTS);
    REQUIRE(n == 3);  // day0, day1 (archived), today (running)
    REQUIRE(out[0].epoch_day == epoch_day_of(DAY0_US));
    REQUIRE(out[0].m3 == Approx(day0_m3));
    REQUIRE(out[1].epoch_day == epoch_day_of(DAY0_US) + 1);
    REQUIRE(out[1].m3 == Approx(day1_m3));
    REQUIRE(out[2].epoch_day == h.svc.today_epoch_day_);  // today last
    REQUIRE(out[2].m3 == Approx(today_m3));
}

TEST_CASE("GasDaily: ring buffer keeps the most recent 64 completed days", "[gas_daily]")
{
    Harness h;
    h.warmup();
    h.boiler_on();

    // Run 70 day transitions with 1 tick per day (accumulating gas each day)
    for (int day = 0; day < 70; day++) {
        h.tick();  // accumulate a little on current day
        h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
        h.tick();  // triggers day archive
    }

    REQUIRE(h.svc.daily_count_ == GasFlowService::DAILY_SLOTS);

    GasFlowService::DailyView out[GasFlowService::DAILY_SLOTS + 1];
    int n = h.svc.get_daily_view(out, GasFlowService::DAILY_SLOTS + 1);
    REQUIRE(n == GasFlowService::DAILY_SLOTS + 1);  // 64 completed + today
    // Oldest archived day must be 64 days before today
    REQUIRE(out[0].epoch_day == h.svc.today_epoch_day_ - GasFlowService::DAILY_SLOTS);
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

// ═══════════════════════════════════════════════════════════════
// Hourly tracking
// ═══════════════════════════════════════════════════════════════

TEST_CASE("GasHourly: init sets today_epoch_hour_, clears accumulator", "[gas_hourly]")
{
    Harness h;
    h.tick();  // last_update_ms_ init only
    REQUIRE(h.svc.today_epoch_hour_ < 0);

    h.tick();  // initializes hour/day tracking
    REQUIRE(h.svc.today_epoch_hour_ == epoch_hour_of(h.time.now_us()));
    REQUIRE(h.svc.hourly_accumulator_ == 0);
}

TEST_CASE("GasHourly: hour boundary archives hour and resets accumulator", "[gas_hourly]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();  // ~1 min in hour 0
    float hour0_m3 = h.svc.hourly_accumulator_;
    REQUIRE(hour0_m3 > 0);

    // Jump to the next hour
    h.time.set_us(h.time.now_us() + 3600ULL * 1000000ULL);
    h.tick();

    REQUIRE(h.svc.today_hours_[0] == Approx(hour0_m3));
    REQUIRE(h.svc.hourly_accumulator_ == 0);
    REQUIRE(h.svc.today_epoch_hour_ == epoch_hour_of(h.time.now_us()));
}

TEST_CASE("GasHourly: clock jump leaves skipped hours as zeros", "[gas_hourly]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 3; i++) h.tick();
    float hour0_m3 = h.svc.hourly_accumulator_;

    // Jump 5 hours forward
    h.time.set_us(h.time.now_us() + 5ULL * 3600ULL * 1000000ULL);
    h.tick();

    REQUIRE(h.svc.today_hours_[0] == Approx(hour0_m3));
    for (int hr = 1; hr <= 4; hr++) REQUIRE(h.svc.today_hours_[hr] == 0);
}

TEST_CASE("GasHourly: day change moves today's hours to yesterday", "[gas_hourly]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();  // ~1 min in hour 0 of day 0
    float hour0_m3 = h.svc.hourly_accumulator_;
    REQUIRE(hour0_m3 > 0);

    // Cross midnight
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();

    REQUIRE(h.svc.yesterday_epoch_day_ == epoch_day_of(DAY0_US));
    REQUIRE(h.svc.yesterday_hours_[0] == Approx(hour0_m3));
    REQUIRE(h.svc.today_hours_[0] == 0);

    GasFlowService::HourlyView out[2 * GasFlowService::HOURS_PER_DAY];
    int n = h.svc.get_hourly_view(out, 2 * GasFlowService::HOURS_PER_DAY);
    REQUIRE(n == 2 * GasFlowService::HOURS_PER_DAY);  // yesterday + today
    REQUIRE(out[0].epoch_hour == epoch_day_of(DAY0_US) * 24);
    REQUIRE(out[0].m3 == Approx(hour0_m3));
    REQUIRE(out[24].epoch_hour == (epoch_day_of(DAY0_US) + 1) * 24);
    REQUIRE(out[24].m3 == 0);
}

TEST_CASE("GasHourly: view includes running current hour", "[gas_hourly]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();
    float running = h.svc.hourly_accumulator_;
    REQUIRE(running > 0);

    GasFlowService::HourlyView out[2 * GasFlowService::HOURS_PER_DAY];
    int n = h.svc.get_hourly_view(out, 2 * GasFlowService::HOURS_PER_DAY);
    // No completed day yet: only today's 24 hours
    REQUIRE(n == GasFlowService::HOURS_PER_DAY);
    REQUIRE(out[0].m3 == Approx(running));
}

TEST_CASE("GasHourly: reset() clears hourly tracking", "[gas_hourly]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();
    REQUIRE(h.svc.yesterday_hours_[0] > 0);

    h.svc.reset();
    REQUIRE(h.svc.today_epoch_hour_ < 0);
    REQUIRE(h.svc.hourly_accumulator_ == 0);
    REQUIRE(h.svc.yesterday_epoch_day_ < 0);
    for (int i = 0; i < GasFlowService::HOURS_PER_DAY; i++) {
        REQUIRE(h.svc.today_hours_[i] == 0);
        REQUIRE(h.svc.yesterday_hours_[i] == 0);
    }
}

TEST_CASE("GasHourly: unsynced wall clock does not initialize hourly tracking", "[gas_hourly]")
{
    Harness h;
    h.time.set_synced(false);
    h.warmup();
    h.boiler_on();
    for (int i = 0; i < 6; i++) h.tick();

    REQUIRE(h.svc.today_epoch_hour_ < 0);
    GasFlowService::HourlyView out[2 * GasFlowService::HOURS_PER_DAY];
    REQUIRE(h.svc.get_hourly_view(out, 2 * GasFlowService::HOURS_PER_DAY) == 0);
    // integral still accumulates normally without wall clock
    REQUIRE(h.svc.integral_m3() > 0);
}

// ═══════════════════════════════════════════════════════════════
// NVS persistence via IGasCorrectionStore (save → reload cycle)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("today tracking persists across simulated reboot", "[gas][daily][persist]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    FakeGasCorrectionStore gcs;

    // Day 0: accumulate some gas
    {
        GasFlowService svc1(state, time, hss, gcs);
        time.set_us(DAY0_US);
        time.set_synced(true);
        state.set_p_max(24.0f);
        state.set_gas_calorific(9.5f);
        state.set_modulation(50.0f);
        state.set_return_temp(45.0f);
        state.set_flame(true);
        for (int i = 0; i < 360; i++) { time.advance_ms(10000); svc1.execute(); }

        GasTodayBlob blob;
        svc1.pack_today(blob);
        gcs.save_today_gas(&blob);
    } // svc1 destroyed — simulated reboot

    // Reboot: new GasFlowService, reload from same gcs
    GasFlowService svc2(state, time, hss, gcs);
    time.set_us(DAY0_US);
    time.set_synced(true);
    svc2.load_daily();

    REQUIRE(svc2.today_epoch_day_ == static_cast<int64_t>(1736899200ULL / 86400));
    REQUIRE(svc2.daily_accumulator_ > 0);
    REQUIRE(svc2.daily_count_ == 0);
    // Completed hour 0 (360 ticks end exactly at 01:00) was restored
    REQUIRE(svc2.today_epoch_hour_ >= 0);
    REQUIRE(svc2.today_hours_[0] > 0);
    REQUIRE(svc2.today_hours_[0] + svc2.hourly_accumulator_ == Approx(svc2.daily_accumulator_));
}

TEST_CASE("history persists across day boundary after reboot", "[gas][daily][persist]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    FakeGasCorrectionStore gcs;

    // Day 0: run, let day roll over to archive day 0's data
    {
        GasFlowService svc1(state, time, hss, gcs);
        time.set_us(DAY0_US);
        time.set_synced(true);
        state.set_p_max(24.0f);
        state.set_gas_calorific(9.5f);
        state.set_modulation(50.0f);
        state.set_return_temp(45.0f);
        state.set_flame(true);
        for (int i = 0; i < 360; i++) { time.advance_ms(10000); svc1.execute(); }

        // Day boundary
        time.set_us(DAY0_US + 86400ULL * 1000000ULL);
        time.advance_ms(10000);
        svc1.execute();
        REQUIRE(svc1.consume_history_dirty());

        GasHistoryBlob hblob;
        svc1.pack_history(hblob);
        gcs.save_history_gas(&hblob);

        GasTodayBlob tblob;
        svc1.pack_today(tblob);
        gcs.save_today_gas(&tblob);
    }

    // Reboot on day 1
    GasFlowService svc2(state, time, hss, gcs);
    time.set_us(DAY0_US + 86400ULL * 1000000ULL);
    time.set_synced(true);
    svc2.load_daily();

    REQUIRE(svc2.daily_count_ == 1);

    GasFlowService::DailyView out[GasFlowService::DAILY_SLOTS];
    int n = svc2.get_daily_view(out, GasFlowService::DAILY_SLOTS);
    REQUIRE(n == 2);
    REQUIRE(out[0].m3 > 0);           // completed day 0
    REQUIRE(out[1].epoch_day == out[0].epoch_day + 1); // today is next day

    // Yesterday's hours were restored
    REQUIRE(svc2.yesterday_epoch_day_ == out[0].epoch_day);
    REQUIRE(svc2.yesterday_hours_[0] > 0);
}

TEST_CASE("history dirty flag is consumed once", "[gas][daily][persist]")
{
    Harness h;
    h.warmup();
    h.boiler_on();
    h.tick();
    h.time.set_us(h.time.now_us() + 86400ULL * 1000000ULL);
    h.tick();

    REQUIRE(h.svc.consume_history_dirty() == true);
    REQUIRE(h.svc.consume_history_dirty() == false);
}

TEST_CASE("load_today/load_history return false when no NVS data (never saved)", "[gas][daily][persist]")
{
    FakeHeatingStateStore state;
    FakeTimeSource time;
    FakeHeatingStatsStore hss;
    FakeGasCorrectionStore gcs;
    GasFlowService svc(state, time, hss, gcs);

    GasTodayBlob tb;
    GasHistoryBlob hb;
    REQUIRE(gcs.load_today_gas(&tb) == false);
    REQUIRE(gcs.load_history_gas(&hb) == false);
}
