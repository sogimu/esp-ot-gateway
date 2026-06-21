#pragma once

#include "application/ports/driven/itime_source.h"

class ILogger;

/// Standalone ITimeSource — NTP-synced wall clock via lwip SNTP.
class SntpTimeAdapter : public ITimeSource {
public:
    SntpTimeAdapter();
    ~SntpTimeAdapter();

    void start();
    void set_timezone(int tz_offset) override;
    int tz_offset() const override { return tz_offset_; }
    void set_servers(const char* srv0, const char* srv1);
    void set_logger(ILogger* log) { logger_ = log; }

    time_point now() const override;
    uint64_t monotonic_us() const override;

    /// Manual time setting (for AP mode without SNTP).
    /// @param epoch_sec  Unix timestamp to set.
    void set_manual_time(time_t epoch_sec);

    /// Persist current boot_offset_us_ to NVS (key "utc_offset_us" in "config").
    void save_time_offset();

    /// Restore boot_offset_us_ from NVS. Returns true if valid offset loaded.
    bool restore_time_offset();

    /// Is time valid (SNTP, manual, or restored from NVS)?
    bool is_synced() const override;

private:
    int  tz_offset_ = 3;
    char srv0_[64] = {};
    char srv1_[64] = {};
    bool started_ = false;
    mutable bool time_synced_ = false;
    mutable uint64_t last_sntp_retry_ms_ = 0;
    mutable microseconds boot_offset_us_{0}; // offset from boot time to Unix epoch
    ILogger* logger_ = nullptr;
    static const char* TAG;
};
