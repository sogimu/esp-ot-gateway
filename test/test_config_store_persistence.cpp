/// Tests for IConfigurationStore::save_burn_stats / load_burn_stats.
/// Regression: burner runtime stats were never persisted, resetting to 0 on reboot.

#include <catch2/catch_test_macros.hpp>
#include "fakes/fake_configuration_store.h"

TEST_CASE("ConfigStore: save_burn_stats then load returns same values", "[config]") {
    FakeConfigurationStore store;

    store.save_burn_stats(3600, 7200, 5, 7000, 1, 200, 4);
    store.set_burn_stats_load(3600, 7200, 5, 7000, 1, 200, 4);

    uint32_t bs = 0, tps = 0, cc = 0, ips = 0, ic = 0, mps = 0, mc = 0;
    REQUIRE(store.load_burn_stats(bs, tps, cc, ips, ic, mps, mc));
    REQUIRE(bs == 3600);
    REQUIRE(tps == 7200);
    REQUIRE(cc == 5);
    REQUIRE(ips == 7000);
    REQUIRE(ic == 1);
    REQUIRE(mps == 200);
    REQUIRE(mc == 4);
}

TEST_CASE("ConfigStore: load_burn_stats returns false when nothing saved", "[config]") {
    FakeConfigurationStore store;

    uint32_t bs = 999, tps = 999, cc = 999, ips = 999, ic = 999, mps = 999, mc = 999;
    REQUIRE_FALSE(store.load_burn_stats(bs, tps, cc, ips, ic, mps, mc));
    REQUIRE(bs == 999);
    REQUIRE(cc == 999);
}

TEST_CASE("ConfigStore: save_total_uptime round-trips", "[config]") {
    FakeConfigurationStore store;

    store.save_total_uptime(86400);
    store.set_total_uptime_load(86400);

    uint32_t v = 0;
    REQUIRE(store.load_total_uptime(v));
    REQUIRE(v == 86400);
}
