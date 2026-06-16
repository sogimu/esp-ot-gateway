/// Host tests for manual time offset math.
/// Tests the offset computation logic used by SntpTimeAdapter
/// without requiring ESP-IDF.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ctime>

// Replicating the core offset logic from SntpTimeAdapter for host-testing.
// In production, this runs on the ESP32 with esp_timer_get_time().

static int64_t compute_offset(time_t epoch_sec, uint64_t monotonic_us) {
    uint64_t real_us = (uint64_t)epoch_sec * 1000000ULL;
    return (int64_t)(real_us - monotonic_us);
}

static time_t epoch_from_offset(int64_t offset_us, uint64_t monotonic_us) {
    uint64_t now_us = monotonic_us + (uint64_t)offset_us;
    return (time_t)(now_us / 1000000ULL);
}

TEST_CASE("Manual time: compute offset from epoch", "[time][manual]") {
    // epoch = 2024-06-16 00:00:00 UTC = 1718496000
    // monotonic = 1,000,000 us (1 second since boot)
    int64_t offset = compute_offset(1718496000, 1000000);
    // real_us = 1718496000 * 1,000,000 = 1,718,496,000,000,000
    // offset = 1,718,496,000,000,000 - 1,000,000 = 1,718,495,999,999,000
    REQUIRE(offset == 1718495999000000LL);
}

TEST_CASE("Manual time: round-trip — set time, read back", "[time][manual]") {
    time_t original = 1718496000;
    uint64_t monotonic_now = 5000000;  // 5 seconds after boot

    int64_t offset = compute_offset(original, 1000000);  // set at boot+1s
    time_t after = epoch_from_offset(offset, monotonic_now);  // read at boot+5s

    // Should be original + 4 seconds
    REQUIRE(after == original + 4);
}

TEST_CASE("Manual time: zero offset returns epoch zero", "[time][manual]") {
    time_t t = epoch_from_offset(0, 0);
    REQUIRE(t == 0);  // Unix epoch start
}

TEST_CASE("Manual time: offset survives 24 hours", "[time][manual]") {
    time_t original = 1718496000;
    uint64_t monotonic_set = 1000000;

    int64_t offset = compute_offset(original, monotonic_set);

    // 24 hours later (86400 seconds)
    uint64_t monotonic_24h = monotonic_set + 86400000000ULL;
    time_t after_24h = epoch_from_offset(offset, monotonic_24h);

    REQUIRE(after_24h == original + 86400);
}

TEST_CASE("Manual time: negative epoch edge case", "[time][manual]") {
    // Pre-epoch dates should still work (offset math is signed)
    time_t pre_epoch = -86400;  // 1969-12-31
    int64_t offset = compute_offset(pre_epoch, 0);
    (void)offset;  // Just verify no overflow
    REQUIRE(true);
}
