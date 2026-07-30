#pragma once

#include "application/ports/driven/itime_source.h"

#include "esp_timer.h"
#include "driver/gpio.h"

class ILogger;

/// Standalone ITimeSource — NTP-synced wall clock via lwip SNTP.
class SntpTimeAdapter : public ITimeSource {
public:
    SntpTimeAdapter(int tz_offset, bool has_internet, ILogger* log = nullptr);
    ~SntpTimeAdapter();

    void set_timezone(int tz_offset) override;
    int tz_offset() const override { return tz_offset_; }
    void set_servers(const char* srv0, const char* srv1);

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

    /// Do we have any valid time reference (SNTP or manual/NVS)?
    bool has_valid_time() const { return boot_offset_us_.count() != 0; }

private:
    void init();   // nvs_flash_init
    void start();
    void configure(int tz_offset, bool has_internet);

    /// Запустить/остановить мигание светодиодом (GPIO2) во время синхронизации.
    void led_blink_start() const;
    void led_blink_stop() const;

    static void led_blink_cb(void* arg);

    int  tz_offset_ = 3;
    char srv0_[64] = {};
    char srv1_[64] = {};
    bool started_ = false;
    mutable bool time_synced_ = false;
    mutable bool sntp_real_synced_ = false;  ///< реальная SNTP-синхронизация (не NVS)
    mutable uint64_t last_sntp_retry_ms_ = 0;
    mutable microseconds boot_offset_us_{0}; // offset from boot time to Unix epoch
    ILogger* logger_ = nullptr;

    mutable esp_timer_handle_t blink_timer_ = nullptr;  ///< таймер мигания LED
    mutable int blink_level_ = 0;                        ///< текущий уровень LED

    static const char* TAG;
};
