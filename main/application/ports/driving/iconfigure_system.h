#pragma once

#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"

/// User-facing system configuration.
/// Called from HTTP /api/control and /api/schedule handlers.
class IConfigureSystem {
public:
    virtual void set_ch_mode(CHMode mode) = 0;
    virtual void set_ch_enable(bool) = 0;
    virtual void set_dhw_enable(bool) = 0;
    virtual void set_ch_setpoint(float temp) = 0;          // validated 20–80
    virtual void set_dhw_setpoint(float temp) = 0;         // validated 35–80
    virtual void set_dhw_hysteresis(float value) = 0;      // validated 0.5–10
    virtual void set_schedule(const CH_Schedule&) = 0;
    virtual void set_pid_schedule(const PID_Schedule&) = 0;
    virtual void set_timezone(int offset) = 0;
    virtual void set_sntp_servers(const char* srv0, const char* srv1) = 0;
    virtual void reset_modulation_stats() = 0;
    virtual void reset_cycle_stats() = 0;
    virtual void reset_gas_stats() = 0;
    virtual ~IConfigureSystem() = default;
};
