#pragma once

#include "domain/value_objects/ch_schedule.h"

/// User-facing system configuration.
/// Called from HTTP /api/control and /api/schedule handlers.
class IConfigureSystem {
public:
    virtual void set_ch_mode(int mode) = 0;               // 0=manual, 1=PID, 2=schedule
    virtual void set_ch_enable(bool) = 0;
    virtual void set_dhw_enable(bool) = 0;
    virtual void set_ch_setpoint(float temp) = 0;          // validated 20–80
    virtual void set_dhw_setpoint(float temp) = 0;         // validated 35–80
    virtual void set_dhw_hysteresis(float value) = 0;      // validated 0.5–10
    virtual void set_schedule(const CH_Schedule&) = 0;
    virtual void set_timezone(int offset) = 0;
    virtual void set_sntp_servers(const char* srv0, const char* srv1) = 0;
    virtual ~IConfigureSystem() = default;
};
