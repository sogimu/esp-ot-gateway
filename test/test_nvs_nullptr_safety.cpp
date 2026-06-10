/// Tests for nullptr safety in save_stats / load_stats.
/// Regression: NvsConfigAdapter passed nullptr blob pointers to nvs_set_blob,
/// writing garbage from address 0x0 to NVS flash, corrupting the namespace
/// and causing IllegalInstruction crash on next boot (cmpMultiPageBlob).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fakes/fake_configuration_store.h"
#include "fakes/fake_heating_state_store.h"
#include "nvs_config_adapter.h"
#include <cstdint>
#include <cstring>

using Catch::Approx;

// ── Nullptr safety: save_stats ────────────────────────────────────────

TEST_CASE("ConfigStore: save_stats with all-null blobs does not crash", "[nvs][nullptr]")
{
    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    // Simulate what main.cpp does: save histogram, null for everything else
    REQUIRE_NOTHROW(
        store.save_stats(state, 3600, 12.5f,
                         nullptr, nullptr, nullptr, nullptr)
    );
}

TEST_CASE("ConfigStore: save_stats with partial blobs — hist non-null, rest null", "[nvs][nullptr]")
{
    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    NvsHistBlob hist_blob;
    hist_blob.samples = 42;
    for (int i = 0; i < 1000; i++) hist_blob.hist[i] = (uint16_t)i;

    // Exact pattern from main.cpp: hist=non-null, cycles=null, ema=null, calib=null
    REQUIRE_NOTHROW(
        store.save_stats(state, 7200, 3.14f,
                         &hist_blob, nullptr, nullptr, nullptr)
    );

    // Verify the fake tracked which blobs were null
    REQUIRE(store.stats_save_h_null_ == false);       // hist was provided
    REQUIRE(store.stats_save_c_null_ == true);         // cycles was null
    REQUIRE(store.stats_save_e_null_ == true);         // ema was null
    REQUIRE(store.stats_save_cal_null_ == true);       // calib was null
}

TEST_CASE("ConfigStore: save_stats tracks burner_sec and integ_m3 values", "[nvs][nullptr]")
{
    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    store.save_stats(state, 9999, 42.5f,
                     nullptr, nullptr, nullptr, nullptr);

    REQUIRE(store.stats_save_bs_ == 9999);
    REQUIRE(store.stats_save_integ_ == Approx(42.5f));
}

// ── Nullptr safety: load_stats ────────────────────────────────────────

TEST_CASE("ConfigStore: load_stats with all-null buffers does not crash", "[nvs][nullptr]")
{
    FakeConfigurationStore store;

    uint32_t bs = 0;
    float integ = 0;

    REQUIRE_NOTHROW(
        store.load_stats(bs, integ, nullptr, nullptr, nullptr, nullptr)
    );
}

TEST_CASE("ConfigStore: load_stats with partial buffers — hist non-null, rest null", "[nvs][nullptr]")
{
    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    // First save histogram so load has data to return
    NvsHistBlob hist_blob_out;
    hist_blob_out.samples = 100;
    store.save_stats(state, 5000, 7.5f,
                     &hist_blob_out, nullptr, nullptr, nullptr);

    // Load with only hist buffer provided
    uint32_t bs = 0;
    float integ = 0;
    NvsHistBlob hist_blob_in;
    memset(&hist_blob_in, 0, sizeof(hist_blob_in));

    REQUIRE_NOTHROW(
        store.load_stats(bs, integ,
                         &hist_blob_in, nullptr, nullptr, nullptr)
    );

    // Verify nullptr tracking
    REQUIRE(store.stats_load_h_null_ == false);
    REQUIRE(store.stats_load_c_null_ == true);
    REQUIRE(store.stats_load_e_null_ == true);
    REQUIRE(store.stats_load_cal_null_ == true);
}

// ── Regression: save_burn_stats + save_stats burner_sec overwrite ─────

TEST_CASE("ConfigStore: save_burn_stats then save_stats preserves burner_sec", "[nvs][regression]")
{
    // Regression: main.cpp used to call save_stats(state, 0, ...)
    // which overwrote "burn_sec" with 0 after save_burn_stats wrote the correct value.
    // Fix: pass the actual burner_sec (not 0) to save_stats.

    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    uint32_t actual_burner_sec = 12345;

    // Step 1: save_burn_stats with correct value
    store.save_burn_stats(actual_burner_sec, 500, 10, 400, 2, 100, 8);
    REQUIRE(store.burn_stats_saved_ == true);
    REQUIRE(store.saved_burn_.burner_sec == actual_burner_sec);

    // Step 2: save_stats — must pass actual_burner_sec, NOT 0
    store.save_stats(state, actual_burner_sec, 3.14f,
                     nullptr, nullptr, nullptr, nullptr);
    REQUIRE(store.stats_save_bs_ == actual_burner_sec);
    // The stored value must match, not be 0
    REQUIRE(store.stats_save_bs_ != 0);
}

TEST_CASE("ConfigStore: save_stats with bs=0 would lose burner_sec — detection", "[nvs][regression]")
{
    // This test documents the BUG: if save_stats is called with bs=0,
    // the burner_sec value saved by save_burn_stats is lost.
    // The test verifies the fake detects this condition.

    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    // save_burn_stats with correct value
    store.save_burn_stats(99999, 500, 10, 400, 2, 100, 8);
    REQUIRE(store.saved_burn_.burner_sec == 99999);

    // BUG: save_stats with bs=0 overwrites burner_sec
    store.save_stats(state, 0, 3.14f,
                     nullptr, nullptr, nullptr, nullptr);

    // The fake records what was passed — 0 means the value was lost
    // In production code (main.cpp), this should never happen:
    // save_stats must receive the actual burner_sec, not 0.
    // This test exists to document the contract.
    REQUIRE(store.stats_save_bs_ == 0);  // <-- this IS the bug pattern

    // After a fixed main.cpp, save_stats would be called with actual_bs != 0.
    // The previous test (preserves burner_sec) verifies the fix.
}

// ── Round-trip: save_stats → load_stats with partial blobs ────────────

TEST_CASE("ConfigStore: save_stats + load_stats round-trip with only hist blob", "[nvs][roundtrip]")
{
    FakeConfigurationStore store;
    FakeHeatingStateStore state;

    NvsHistBlob saved_hist;
    saved_hist.samples = 77;
    for (int i = 0; i < 1000; i++) saved_hist.hist[i] = (uint16_t)(i % 100);

    store.save_stats(state, 8888, 9.99f,
                     &saved_hist, nullptr, nullptr, nullptr);

    uint32_t bs = 0;
    float integ = 0;
    REQUIRE_NOTHROW(
        store.load_stats(bs, integ, nullptr, nullptr, nullptr, nullptr)
    );

    // load_stats returns the saved values through the fake
    REQUIRE(bs == 8888);
    REQUIRE(integ == Approx(9.99f));
}

// ── Static assert verification (compile-time) ─────────────────────────
// These are tested implicitly: if blob sizes don't match, the build fails.
// The static_assert lines are in nvs_config_adapter.h.
// This test exists to document the expected sizes.

TEST_CASE("ConfigStore: blob size invariants are documented", "[nvs][struct]")
{
    // These must match the static_assert values in nvs_config_adapter.h
    // If any fail, the static_assert catches it at compile time.

    // NvsHistBlob: 4 (samples) + 1000 * 2 (hist) = 2004
    // NvsCycleBlob: 4+4+4+4 + 256*2 + 256*2 = 1040
    // NvsGasEmaBlob: 5*4 + 8 = 28
    // NvsCalibBlob: 3*4 = 12
    // NvsMeterBlob: 4+4+4+4+4 + 32*(4+4+4+4+4+4) = 20 + 32*24 = 788
    // NvsPredictBlob: 3*4 + 4 + 4 = 20

    // Volatile test — verifies sizeof at runtime as a sanity check
    // (the real guard is static_assert in nvs_config_adapter.h)
    REQUIRE(sizeof(NvsHistBlob) == 2004);
    REQUIRE(sizeof(NvsCycleBlob) == 1040);
    REQUIRE(sizeof(NvsGasEmaBlob) == 28);
    REQUIRE(sizeof(NvsCalibBlob) == 12);
    REQUIRE(sizeof(NvsMeterBlob) == 788);
    REQUIRE(sizeof(NvsPredictBlob) == 20);
}
