#pragma once

/// CH operating mode — persisted in NVS, sent over JSON as int.
enum class CHMode : int {
    Manual_Static = 0,   // Manual mode, constant CH setpoint
    PID_Static    = 1,   // Adaptive (PID) mode, constant room target
    Manual_Sched  = 2,   // Manual mode, schedule-based CH setpoint
    PID_Sched     = 3    // Adaptive (PID) mode, schedule-based room target
};

inline bool is_pid_mode(CHMode m) {
    return m == CHMode::PID_Static || m == CHMode::PID_Sched;
}
