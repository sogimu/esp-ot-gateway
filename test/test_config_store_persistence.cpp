/// Tests for IConfigurationStore::save_burner_sec / load_burner_sec.
/// Regression: burner runtime stats were never persisted, resetting to 0 on reboot.

#include <catch2/catch_test_macros.hpp>
#include "fakes/fake_configuration_store.h"

TEST_CASE("ConfigStore: save_burner_sec then load returns same values", "[config]") {
    FakeConfigurationStore store;

    store.save_burner_sec(3600, 5);
    store.set_burner_sec_load(3600, 5);

    uint32_t bs = 0, cc = 0;
    REQUIRE(store.load_burner_sec(bs, cc));
    REQUIRE(bs == 3600);
    REQUIRE(cc == 5);
}

TEST_CASE("ConfigStore: load_burner_sec returns false when nothing saved", "[config]") {
    FakeConfigurationStore store;

    uint32_t bs = 999, cc = 999;
    REQUIRE_FALSE(store.load_burner_sec(bs, cc));
    REQUIRE(bs == 999);
    REQUIRE(cc == 999);
}

TEST_CASE("ConfigStore: save_burner_sec with zero values round-trips", "[config]") {
    FakeConfigurationStore store;

    store.save_burner_sec(0, 0);
    store.set_burner_sec_load(0, 0);

    uint32_t bs = 99, cc = 99;
    REQUIRE(store.load_burner_sec(bs, cc));
    REQUIRE(bs == 0);
    REQUIRE(cc == 0);
}
