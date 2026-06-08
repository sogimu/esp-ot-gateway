#pragma once

#include <stdint.h>

/// Time source abstraction — for timestamps and interval measurement.
class ITimeSource {
public:
    /// Microseconds since boot (monotonic, for intervals).
    virtual uint64_t now_us() const = 0;

    /// Seconds since epoch (wall clock, for event timestamps).
    /// May be 0 if NTP hasn't synced yet.
    virtual uint32_t now_sec() const = 0;

    /// Update system timezone offset (affects localtime_r).
    virtual void set_timezone(int offset) = 0;

    virtual ~ITimeSource() = default;
};
