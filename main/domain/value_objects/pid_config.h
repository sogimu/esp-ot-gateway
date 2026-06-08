#pragma once

/// PID controller configuration — value object.
class PidConfig {
public:
    PidConfig() = default;
    PidConfig(float kp, float ki, float kd, int dt_sec,
              int room_sensor, float target_room, int lockout_sec);

    float kp()          const { return kp_; }
    float ki()          const { return ki_; }
    float kd()          const { return kd_; }
    int   dt_sec()      const { return dt_sec_; }
    int   room_sensor() const { return room_sensor_; }
    float target_room() const { return target_room_; }
    int   lockout_sec() const { return lockout_sec_; }

    bool is_valid() const;

private:
    float kp_ = 2.0f;
    float ki_ = 0.01f;
    float kd_ = 0.0f;
    int   dt_sec_ = 60;
    int   room_sensor_ = 0;
    float target_room_ = 22.0f;
    int   lockout_sec_ = 300;
};
