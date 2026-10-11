#pragma once

#include <cstdint>
#include "application/ports/driving/icontrol_task.h"

class IBoilerHardware;
class IHeatingStateStore;
class ILogger;
class ITimeSource;

/// Full OpenTherm poll cycle: handshake → DHW hysteresis → status → extra_step → connectivity.
/// Replaces OpenthermEndpoint::poll_cycle() when activated.
class BoilerPollInteractor : public IControlTask {
public:
    BoilerPollInteractor(IBoilerHardware& boiler, IHeatingStateStore& state, ILogger& log, ITimeSource& time);

    void execute() override;

    // Configuration setters (called from SystemConfigInteractor or HTTP)
    void set_ch_enable(bool en);
    void set_dhw_enable(bool en);
    void set_ch_setpoint(float sp);
    void set_dhw_setpoint(float sp);
    void set_dhw_hysteresis(float hyst);
    void trigger_fault_reset();

private:
    IBoilerHardware&   boiler_;
    IHeatingStateStore& state_;
    ILogger&           log_;
    ITimeSource&       time_;

    // Poll state
    int  poll_step_ = 0;
    bool handshake_done_ = false;
    bool handshake_attempted_ = false;
    uint32_t last_handshake_ms_ = 0;
    uint32_t last_response_ms_ = 0;
    bool connected_ = false;

    // Pending writes
    bool  pending_ch_enable_ = false, pending_ch_enable_val_ = false;
    bool  pending_dhw_enable_ = false, pending_dhw_enable_val_ = false;
    bool  pending_fault_reset_ = false;
    float pending_ch_sp_ = 30.0f;  bool pending_ch_sp_dirty_ = false;
    float pending_dhw_sp_ = 55.0f; bool pending_dhw_sp_dirty_ = false;

    // DHW hysteresis
    bool  dhw_priority_ = false;
    uint32_t dhw_session_start_ms_ = 0;
    float dhw_session_min_temp_ = 0;

    // Previous status for change detection
    bool last_fault_ = false, last_flame_ = false;
    bool last_ch_active_ = false, last_dhw_active_ = false;

    // Sub-steps
    void do_handshake();
    void do_dhw_hysteresis();
    void do_status();
    void do_extra_step();
    void check_connectivity();
    uint8_t build_master_byte();

    // Helpers
    uint32_t now_ms() const;
    static float f88_to_float(uint16_t v);
    static uint16_t float_to_f88(float f);
};
