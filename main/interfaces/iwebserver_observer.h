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
    virtual void on_cmd_set_k_calib(float value) = 0;

    virtual void on_cmd_reset_modulation_stats() = 0;
    virtual void on_cmd_reset_cycle_stats() = 0;
    virtual void on_cmd_reset_gas_stats() = 0;
};
