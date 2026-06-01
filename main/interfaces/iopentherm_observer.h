#pragma once

class IOpenthermObserver {
public:
    virtual ~IOpenthermObserver() = default;

    virtual void on_connected() = 0;
    virtual void on_disconnected() = 0;
    virtual void on_status_changed(bool fault, bool flame,
                                   bool ch_active, bool dhw_active) = 0;
    virtual void on_ch_temp(float value) = 0;
    virtual void on_dhw_temp(float value) = 0;
    virtual void on_return_temp(float value) = 0;
    virtual void on_outside_temp(float value) = 0;
    virtual void on_modulation(float pct) = 0;
    virtual void on_ch_bounds(float min, float max) = 0;
    virtual void on_dhw_bounds(float min, float max) = 0;
    virtual void on_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) = 0;
    virtual void on_runtime_counters(uint16_t burner_starts, uint16_t ch_pump_starts,
                                     uint16_t dhw_valve_starts, uint16_t dhw_burner_starts) = 0;
    virtual void on_runtime_hours(uint16_t burner_hrs, uint16_t ch_pump_hrs,
                                  uint16_t dhw_valve_hrs, uint16_t dhw_burner_hrs) = 0;
    virtual void on_version(uint8_t slave_type, uint8_t slave_ver, float ot_ver) = 0;
    virtual void on_dhw_session_finished(uint32_t duration_ms, float min_temp) = 0;
    virtual void on_dhw_session_started(float start_temp) {}
    virtual void on_ch_setpoint_confirmed(float value) = 0;
    virtual void on_dhw_setpoint_confirmed(float value) = 0;
};
