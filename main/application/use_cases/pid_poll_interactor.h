#pragma once

#include <cstdint>
#include "application/ports/driving/ipollable.h"
#include "domain/services/pid_algorithm.h"

class IHeatingStateStore;
class IBoilerHardware;
class ITimeSource;
class ILogger;

/// PID controller tick — evaluates PID once per dt_sec.
/// Reads room temperature from sensor, computes PID output, writes CH setpoint.
class PidPollInteractor : public IPollable {
public:
    PidPollInteractor(IHeatingStateStore& state, IBoilerHardware& boiler,
                      ITimeSource& time, ILogger& log);

    void poll() override;

    /// Enable PID control. Resets tracking state, updates state store.
    void enable();
    /// Disable PID control. Resets tracking state, updates state store.
    void disable();

private:
    IHeatingStateStore& state_;
    IBoilerHardware&    boiler_;
    ITimeSource&        time_;
    ILogger&            log_;

    PidAlgoCfg   pid_cfg_{};
    PidAlgoState pid_state_{};
    bool pid_inited_ = false;

    uint32_t last_compute_ms_ = 0;
    bool     prev_flame_ = false;
    uint32_t last_flame_off_ms_ = 0;
    bool     cycle_locked_ = false;
    bool     lockout_logged_ = false;
    bool     overheat_logged_ = false;
    bool     clamped_logged_ = false;

    void load_config();
    void compute_pid();
    void apply_output(float output);
};
