#pragma once

/// PID controller configuration commands.
class IConfigurePid {
public:
    virtual void set_pid_enable(bool) = 0;
    virtual void set_pid_parameters(float kp, float ki, float kd,
                                     int dt_sec, int room_sensor,
                                     float target_room, int lockout_sec) = 0;
    virtual void set_pid_hysteresis(float) = 0;
    virtual ~IConfigurePid() = default;
};
