#pragma once

/// Pure function: 24h schedule application.
class ScheduleService {
public:
    struct Result {
        bool has_setpoint;  // true if a setpoint should be applied
        float setpoint;     // CH setpoint value (20-80), valid if has_setpoint
    };

    /// Evaluate schedule for current hour.
    /// @param schedule_enabled  whether schedule mode is active
    /// @param temps             array of 24 hourly temperatures
    /// @param current_hour      0-23
    /// @return Result with setpoint or has_setpoint=false
    static Result evaluate(bool schedule_enabled, const float temps[24], int current_hour);

    /// Validate that a setpoint is within CH range (20-80 C).
    static bool is_valid_setpoint(float sp);
};
