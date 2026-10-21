#pragma once

/// Pure function: DHW heating on/off schedule application.
class DhwScheduleService {
public:
    /// Returns true if DHW heating is allowed at the given local hour.
    /// @param on    array of 24 hourly on/off flags
    /// @param hour  local hour 0-23, or -1 when time is invalid
    /// @return true when heating allowed (fail-open when hour invalid)
    static bool is_allowed(const bool on[24], int hour);
};
