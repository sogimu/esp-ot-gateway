#pragma once

#include <chrono>
#include <cstdint>

/// Time source abstraction — for timestamps and interval measurement.
class ITimeSource {
public:
    using clock      = std::chrono::system_clock;
    using time_point = clock::time_point;
    using microseconds = std::chrono::microseconds;
    using milliseconds = std::chrono::milliseconds;
    using seconds      = std::chrono::seconds;

    /// Current time point (wall clock, synced via NTP).
    virtual time_point now() const = 0;

    /// Convenience: microseconds since epoch.
    uint64_t now_us() const {
        return std::chrono::duration_cast<microseconds>(
            now().time_since_epoch()).count();
    }
    /// Convenience: milliseconds since epoch.
    uint64_t now_ms() const {
        return std::chrono::duration_cast<milliseconds>(
            now().time_since_epoch()).count();
    }

    /// Monotonic microseconds since boot (for interval measurement).
    /// Not affected by NTP sync or wall-clock adjustments.
    virtual uint64_t monotonic_us() const = 0;
    /// Monotonic milliseconds since boot.
    uint64_t monotonic_ms() const { return monotonic_us() / 1000; }
    /// Convenience: seconds since epoch (0 if NTP not synced).
    uint32_t now_s() const {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<seconds>(
                now().time_since_epoch()).count());
    }

    /// Timezone offset from UTC (hours).
    virtual int tz_offset() const = 0;
    /// Current local time (UTC + timezone).
    time_point local_now() const {
        return now() + std::chrono::hours(tz_offset());
    }

    /// Update system timezone offset.
    virtual void set_timezone(int offset) = 0;

    /// Has time been synchronised (SNTP or manual)?
    virtual bool is_synced() const = 0;

    virtual ~ITimeSource() = default;
};
