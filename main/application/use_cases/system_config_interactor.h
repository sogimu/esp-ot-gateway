#pragma once

#include "application/ports/driving/iconfigure_system.h"
#include "application/ports/driving/iconfigure_pid.h"
#include "application/ports/driving/ifault_reset.h"
#include "application/ports/driving/ireset_statistics.h"

class IHeatingStateStore;
class IBoilerHardware;
class ITimeSettingsStore;
class IBoilerConfigStore;
class ILogger;
class ITimeSource;
class BoilerPollInteractor;
class PidPollInteractor;
class ModulationStatsService;
class BurnCycleService;
class GasFlowService;

/// Implements all user-facing configuration use cases.
class SystemConfigInteractor : public IConfigureSystem, public IConfigurePid, public IFaultReset, public IResetStatistics {
public:
    SystemConfigInteractor(IHeatingStateStore& state, IBoilerHardware& boiler,
                           ITimeSettingsStore& config, IBoilerConfigStore& boiler_cfg,
                           ILogger& log, ITimeSource& time,
                           BoilerPollInteractor* boiler_poll = nullptr,
                           PidPollInteractor*   pid_poll = nullptr,
                           BurnCycleService*    burn_cycles = nullptr,
                           ModulationStatsService* mod_stats = nullptr,
                           GasFlowService*      gas_flow = nullptr);

    // IConfigureSystem
    void set_ch_mode(CHMode mode) override;
    void set_ch_enable(bool) override;
    void set_dhw_enable(bool) override;
    void set_ch_setpoint(float) override;
    void set_dhw_setpoint(float) override;
    void set_dhw_hysteresis(float) override;
    void set_schedule(const CH_Schedule&) override;
    void set_pid_schedule(const PID_Schedule&) override;
    void set_timezone(int offset) override;
    void set_sntp_servers(const char* srv0, const char* srv1) override;

    // IConfigurePid
    void set_pid_enable(bool) override;
    void set_pid_parameters(float kp, float ki, float kd, int dt_sec,
                             int room_sensor, float target_room, int lockout_sec) override;
    void set_pid_hysteresis(float) override;

    // IFaultReset
    void reset() override;

    // IResetStatistics
    void reset_modulation_stats() override;
    void reset_cycle_stats() override;
    void reset_gas_stats() override;

private:
    IHeatingStateStore&  state_;
    IBoilerHardware&     boiler_;
    ITimeSettingsStore&  config_;
    IBoilerConfigStore&  boiler_cfg_;
    ILogger&             log_;
    ITimeSource&         time_;
    BoilerPollInteractor* boiler_poll_ = nullptr;
    PidPollInteractor*   pid_poll_ = nullptr;
    BurnCycleService*    burn_cycles_ = nullptr;
    ModulationStatsService* mod_stats_ = nullptr;
    GasFlowService*      gas_flow_ = nullptr;

    void save_and_log(const char* msg, ...);
};
