#pragma once

#include <stdint.h>
#include "model/model.h"

class IWebServerObserver {
public:
    virtual ~IWebServerObserver() = default;

    virtual void on_cmd_set_ch_enable(bool enable) = 0;
    virtual void on_cmd_set_dhw_enable(bool enable) = 0;
    virtual void on_cmd_set_ch_setpoint(float temp) = 0;
    virtual void on_cmd_set_dhw_setpoint(float temp) = 0;
    virtual void on_cmd_fault_reset() = 0;
    virtual void on_cmd_set_schedule(const CH_Schedule& schedule) = 0;
    virtual void on_cmd_set_timezone(int offset) = 0;
    virtual void on_cmd_set_dhw_hysteresis(float value) = 0;
    virtual void on_cmd_set_sntp_servers(const char* srv0, const char* srv1) = 0;

    virtual void on_cmd_set_k_calib(float value) = 0;
    virtual void on_cmd_set_gas_meter_base(float value) = 0;
    virtual void on_cmd_add_gas_meter_correction(float reading) = 0;

    virtual void on_cmd_reset_modulation_stats() = 0;
    virtual void on_cmd_reset_cycle_stats() = 0;
    virtual void on_cmd_reset_gas_stats() = 0;

    virtual void on_cmd_set_pid_enable(bool enable) = 0;
    virtual void on_cmd_set_pid_kp(float value) = 0;
    virtual void on_cmd_set_pid_ki(float value) = 0;
    virtual void on_cmd_set_pid_kd(float value) = 0;
    virtual void on_cmd_set_pid_dt_sec(int value) = 0;
    virtual void on_cmd_set_pid_room_sensor(int value) = 0;
    virtual void on_cmd_set_pid_target_room(float value) = 0;
    virtual void on_cmd_set_pid_cycle_lockout_sec(int value) = 0;
};
