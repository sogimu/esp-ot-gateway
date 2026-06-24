#pragma once

#include "application/ports/driven/iheating_state_store.h"
#include <cstring>
#include <string>

/// Fake state store for unit testing — no FreeRTOS, no mutexes.
/// Stores all fields inline, lock methods are no-ops.
class FakeHeatingStateStore : public IHeatingStateStore {
public:
    FakeHeatingStateStore() = default;

    void reset_to_defaults() {
        connected_ = false; fault_ = false; flame_ = false; ch_active_ = false; dhw_active_ = false;
        ch_temp_ = 0; dhw_temp_ = 0; return_temp_ = 0; outside_temp_ = 0; modulation_ = 0;
        t1_temp_ = -127.0f; t2_temp_ = -127.0f;
        ch_setpoint_ = 30.0f; dhw_setpoint_ = 55.0f;
        ch_sp_min_ = 0; ch_sp_max_ = 80; dhw_sp_min_ = 0; dhw_sp_max_ = 65;
        ch_enable_ = false; dhw_enable_ = false;
        asf_flags_ = 0; oem_fault_ = 0; oem_diag_ = 0;
        burner_starts_ = 0; ch_pump_starts_ = 0; dhw_valve_starts_ = 0; dhw_burner_starts_ = 0;
        burner_hours_ = 0; ch_pump_hours_ = 0; dhw_valve_hours_ = 0; dhw_burner_hours_ = 0;
        slave_type_ = 0; slave_version_ = 0; ot_version_ = 0;
        dhw_last_session_sec_ = 0; dhw_pred_active_ = false;
        dhw_pred_remaining_ = 0; dhw_pred_uncertainty_ = 0; dhw_pred_elapsed_ = 0; dhw_pred_rate_ = 0;
        pid_enabled_ = false; pid_active_ = false; pid_output_ = 0; pid_p_ = 0; pid_i_ = 0; pid_d_ = 0;
        pid_room_temp_ = 0; pid_target_room_ = 0; pid_cycle_locked_ = false;
        pid_remaining_lockout_ = 0; pid_ch_enabled_ = false;
        pid_kp_ = 2.0f; pid_ki_ = 0.01f; pid_kd_ = 0;
        pid_dt_sec_ = 60; pid_room_sensor_ = 0; pid_lockout_sec_ = 300; pid_hysteresis_ = 0.5f;
        ch_mode_ = CHMode::Manual_Static;
        schedule_enabled_ = false;
        for (int i = 0; i < 24; i++) schedule_temps_[i] = 0;
        pid_schedule_enabled_ = false;
        for (int i = 0; i < 24; i++) pid_schedule_temps_[i] = 0;
        dhw_hysteresis_ = 2.0f;
        tz_offset_ = 3;
        sntp_srv0_.clear(); sntp_srv1_.clear();
        k_calib_ = 1.0f; p_max_ = 24.0f; gas_calorific_ = 9.5f; gas_meter_base_ = 0;
        gas_temp_offset_ = -5.0f;
        ch_pmin_ = 5.5f; ch_pmax_ = 24.0f;
        dhw_pmin_ = 5.5f; dhw_pmax_ = 24.0f;
        eff_t1_ = 30.0f; eff_v1_ = 0.98f;
        eff_t2_ = 55.0f; eff_v2_ = 0.93f;
        eff_t3_ = 80.0f; eff_v3_ = 0.88f;
    }

    // Lock protocol (no-op)
    void lock_shared() override {}
    void unlock_shared() override {}
    void lock_exclusive() override {}
    void unlock_exclusive() override {}

    // Boiler status
    void set_connected(bool v) override { connected_ = v; }
    void set_fault(bool v) override { fault_ = v; }
    void set_flame(bool v) override { flame_ = v; }
    void set_ch_active(bool v) override { ch_active_ = v; }
    void set_dhw_active(bool v) override { dhw_active_ = v; }
    bool is_connected()  const override { return connected_; }
    bool has_fault()     const override { return fault_; }
    bool is_flame_on()   const override { return flame_; }
    bool is_ch_active()  const override { return ch_active_; }
    bool is_dhw_active() const override { return dhw_active_; }

    // Temperatures
    void set_ch_temp(float v) override { ch_temp_ = v; }
    void set_dhw_temp(float v) override { dhw_temp_ = v; }
    void set_return_temp(float v) override { return_temp_ = v; }
    void set_outside_temp(float v) override { outside_temp_ = v; }
    void set_modulation(float v) override { modulation_ = v; }
    void set_t1_temp(float v) override { t1_temp_ = v; }
    void set_t2_temp(float v) override { t2_temp_ = v; }
    float get_ch_temp()     const override { return ch_temp_; }
    float get_dhw_temp()    const override { return dhw_temp_; }
    float get_return_temp() const override { return return_temp_; }
    float get_outside_temp() const override { return outside_temp_; }
    float get_modulation()  const override { return modulation_; }
    float get_t1_temp()     const override { return t1_temp_; }
    float get_t2_temp()     const override { return t2_temp_; }

    // Setpoints & bounds
    void set_ch_setpoint(float v) override { ch_setpoint_ = v; }
    void set_dhw_setpoint(float v) override { dhw_setpoint_ = v; }
    void set_ch_sp_min(float v) override { ch_sp_min_ = v; }
    void set_ch_sp_max(float v) override { ch_sp_max_ = v; }
    void set_dhw_sp_min(float v) override { dhw_sp_min_ = v; }
    void set_dhw_sp_max(float v) override { dhw_sp_max_ = v; }
    float get_ch_setpoint()  const override { return ch_setpoint_; }
    float get_dhw_setpoint() const override { return dhw_setpoint_; }
    float get_ch_sp_min()    const override { return ch_sp_min_; }
    float get_ch_sp_max()    const override { return ch_sp_max_; }
    float get_dhw_sp_min()   const override { return dhw_sp_min_; }
    float get_dhw_sp_max()   const override { return dhw_sp_max_; }

    // Enables
    void set_ch_enable(bool v) override { ch_enable_ = v; }
    void set_dhw_enable(bool v) override { dhw_enable_ = v; }
    bool is_ch_enabled()  const override { return ch_enable_; }
    bool is_dhw_enabled() const override { return dhw_enable_; }

    // Fault codes
    void set_fault_codes(uint8_t asf, uint8_t oem_f, uint16_t oem_d) override {
        asf_flags_ = asf; oem_fault_ = oem_f; oem_diag_ = oem_d;
    }
    uint8_t  get_asf_flags()       const override { return asf_flags_; }
    uint8_t  get_oem_fault_code()  const override { return oem_fault_; }
    uint16_t get_oem_diagnostic()  const override { return oem_diag_; }

    // Runtime counters
    void set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) override {
        burner_starts_ = bs; ch_pump_starts_ = cps; dhw_valve_starts_ = dvs; dhw_burner_starts_ = dbs;
    }
    void set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) override {
        burner_hours_ = bh; ch_pump_hours_ = cph; dhw_valve_hours_ = dvh; dhw_burner_hours_ = dbh;
    }
    #define FAKE_GET_U16(name) uint16_t get_##name() const override { return name##_; }
    FAKE_GET_U16(burner_starts) FAKE_GET_U16(ch_pump_starts)
    FAKE_GET_U16(dhw_valve_starts) FAKE_GET_U16(dhw_burner_starts)
    FAKE_GET_U16(burner_hours) FAKE_GET_U16(ch_pump_hours)
    FAKE_GET_U16(dhw_valve_hours) FAKE_GET_U16(dhw_burner_hours)

    // Version
    void set_version(uint8_t st, uint8_t sv, float ov) override {
        slave_type_ = st; slave_version_ = sv; ot_version_ = ov;
    }
    uint8_t get_slave_type()    const override { return slave_type_; }
    uint8_t get_slave_version() const override { return slave_version_; }
    float   get_ot_version()    const override { return ot_version_; }

    // DHW session & prediction
    void set_dhw_session_finished(uint32_t dur_ms, float) override { dhw_last_session_sec_ = (int)(dur_ms / 1000); }
    void set_dhw_prediction(bool active, int rem, int unc, float rate, int elapsed) override {
        dhw_pred_active_ = active; dhw_pred_remaining_ = rem;
        dhw_pred_uncertainty_ = unc; dhw_pred_rate_ = rate; dhw_pred_elapsed_ = elapsed;
    }
    int  get_dhw_last_session_sec()      const override { return dhw_last_session_sec_; }
    bool get_dhw_pred_active()           const override { return dhw_pred_active_; }
    int  get_dhw_pred_remaining_sec()    const override { return dhw_pred_remaining_; }
    int  get_dhw_pred_uncertainty_sec()  const override { return dhw_pred_uncertainty_; }
    int  get_dhw_pred_elapsed_sec()      const override { return dhw_pred_elapsed_; }
    float get_dhw_pred_rate_cps()        const override { return dhw_pred_rate_; }

    // PID state
    void set_pid_state(bool en, bool act, float out, float p, float i, float d,
                       float rt, float tr, bool cl, int rl, bool ce) override {
        pid_enabled_ = en; pid_active_ = act; pid_output_ = out;
        pid_p_ = p; pid_i_ = i; pid_d_ = d;
        pid_room_temp_ = rt; pid_target_room_ = tr;
        pid_cycle_locked_ = cl; pid_remaining_lockout_ = rl; pid_ch_enabled_ = ce;
    }
    void set_pid_config(float kp, float ki, float kd, int dt, int rs, float tr, int lo) override {
        pid_kp_ = kp; pid_ki_ = ki; pid_kd_ = kd;
        pid_dt_sec_ = dt; pid_room_sensor_ = rs;
        pid_target_room_ = tr; pid_lockout_sec_ = lo;
    }
    void set_pid_hysteresis(float v) override { pid_hysteresis_ = v; }
    #define FAKE_PID_GET(type, name) type get_pid_##name() const override { return pid_##name##_; }
    FAKE_PID_GET(bool, enabled) FAKE_PID_GET(bool, active) FAKE_PID_GET(float, output)
    FAKE_PID_GET(float, p) FAKE_PID_GET(float, i) FAKE_PID_GET(float, d)
    FAKE_PID_GET(float, room_temp) FAKE_PID_GET(float, target_room)
    FAKE_PID_GET(bool, cycle_locked) FAKE_PID_GET(int, remaining_lockout)
    FAKE_PID_GET(float, kp) FAKE_PID_GET(float, ki) FAKE_PID_GET(float, kd)
    FAKE_PID_GET(int, dt_sec) FAKE_PID_GET(int, room_sensor) FAKE_PID_GET(int, lockout_sec)
    bool get_pid_ch_enabled_by_pid() const override { return pid_ch_enabled_; }
    FAKE_PID_GET(float, hysteresis)

    // CH mode
    void set_ch_mode(CHMode v) override { ch_mode_ = v; }
    CHMode get_ch_mode() const override { return ch_mode_; }

    // Schedule
    void set_schedule(const void* sched) override {
        struct CH_Schedule { bool enabled; float temps[24]; };
        const auto& s = *static_cast<const CH_Schedule*>(sched);
        schedule_enabled_ = s.enabled;
        std::memcpy(schedule_temps_, s.temps, 24 * sizeof(float));
    }
    void get_schedule(void* sched) const override {
        struct CH_Schedule { bool enabled; float temps[24]; };
        auto& s = *static_cast<CH_Schedule*>(sched);
        s.enabled = schedule_enabled_;
        std::memcpy(s.temps, schedule_temps_, 24 * sizeof(float));
    }
    void set_pid_schedule(const void* sched) override {
        struct PID_Schedule { bool enabled; float temps[24]; };
        const auto& s = *static_cast<const PID_Schedule*>(sched);
        pid_schedule_enabled_ = s.enabled;
        std::memcpy(pid_schedule_temps_, s.temps, 24 * sizeof(float));
    }
    void get_pid_schedule(void* sched) const override {
        struct PID_Schedule { bool enabled; float temps[24]; };
        auto& s = *static_cast<PID_Schedule*>(sched);
        s.enabled = pid_schedule_enabled_;
        std::memcpy(s.temps, pid_schedule_temps_, 24 * sizeof(float));
    }

    // Hysteresis
    void  set_dhw_hysteresis(float v) override { dhw_hysteresis_ = v; }
    float get_dhw_hysteresis() const override { return dhw_hysteresis_; }

    // Timezone & SNTP
    void set_tz_offset(int v) override { tz_offset_ = v; }
    int  get_tz_offset() const override { return tz_offset_; }
    void set_sntp_server0(const char* v) override { sntp_srv0_ = v ? v : ""; }
    void set_sntp_server1(const char* v) override { sntp_srv1_ = v ? v : ""; }
    const char* get_sntp_server0() const override { return sntp_srv0_.c_str(); }
    const char* get_sntp_server1() const override { return sntp_srv1_.c_str(); }

    // Gas / calibration
    void set_k_calib(float v) override { k_calib_ = v; }
    float get_k_calib() const override { return k_calib_; }
    void set_p_max(float v) override { p_max_ = v; }
    float get_p_max() const override { return p_max_; }
    void set_gas_calorific(float v) override { gas_calorific_ = v; }
    float get_gas_calorific() const override { return gas_calorific_; }
    void set_gas_meter_base(float v) override { gas_meter_base_ = v; }
    float get_gas_meter_base() const override { return gas_meter_base_; }

    void set_mqtt_connected(bool v) override { mqtt_connected_ = v; }
    bool is_mqtt_connected() const override  { return mqtt_connected_; }

    // Boiler model config
    void set_gas_temp_offset(float v) override { gas_temp_offset_ = v; }
    float get_gas_temp_offset() const override { return gas_temp_offset_; }
    void set_ch_pmin(float v) override { ch_pmin_ = v; }
    float get_ch_pmin() const override { return ch_pmin_; }
    void set_ch_pmax(float v) override { ch_pmax_ = v; }
    float get_ch_pmax() const override { return ch_pmax_; }
    void set_dhw_pmin(float v) override { dhw_pmin_ = v; }
    float get_dhw_pmin() const override { return dhw_pmin_; }
    void set_dhw_pmax(float v) override { dhw_pmax_ = v; }
    float get_dhw_pmax() const override { return dhw_pmax_; }
    void set_eff_t1(float v) override { eff_t1_ = v; }
    float get_eff_t1() const override { return eff_t1_; }
    void set_eff_v1(float v) override { eff_v1_ = v; }
    float get_eff_v1() const override { return eff_v1_; }
    void set_eff_t2(float v) override { eff_t2_ = v; }
    float get_eff_t2() const override { return eff_t2_; }
    void set_eff_v2(float v) override { eff_v2_ = v; }
    float get_eff_v2() const override { return eff_v2_; }
    void set_eff_t3(float v) override { eff_t3_ = v; }
    float get_eff_t3() const override { return eff_t3_; }
    void set_eff_v3(float v) override { eff_v3_ = v; }
    float get_eff_v3() const override { return eff_v3_; }
    // ── Public fields for direct test inspection ─────────
    bool   connected_ = false, fault_ = false, flame_ = false, ch_active_ = false, dhw_active_ = false;
    float  ch_temp_ = 0, dhw_temp_ = 0, return_temp_ = 0, outside_temp_ = 0, modulation_ = 0;
    float  t1_temp_ = -127.0f, t2_temp_ = -127.0f;
    float  ch_setpoint_ = 30.0f, dhw_setpoint_ = 55.0f;
    float  ch_sp_min_ = 0, ch_sp_max_ = 80;
    float  dhw_sp_min_ = 0, dhw_sp_max_ = 65;
    bool   ch_enable_ = false, dhw_enable_ = false;
    uint8_t  asf_flags_ = 0, oem_fault_ = 0;
    uint16_t oem_diag_ = 0;
    uint16_t burner_starts_ = 0, ch_pump_starts_ = 0, dhw_valve_starts_ = 0, dhw_burner_starts_ = 0;
    uint16_t burner_hours_ = 0, ch_pump_hours_ = 0, dhw_valve_hours_ = 0, dhw_burner_hours_ = 0;
    uint8_t  slave_type_ = 0, slave_version_ = 0;
    float    ot_version_ = 0;
    int      dhw_last_session_sec_ = 0;
    bool     dhw_pred_active_ = false;
    int      dhw_pred_remaining_ = 0, dhw_pred_uncertainty_ = 0, dhw_pred_elapsed_ = 0;
    float    dhw_pred_rate_ = 0;
    bool   pid_enabled_ = false, pid_active_ = false;
    float  pid_output_ = 0, pid_p_ = 0, pid_i_ = 0, pid_d_ = 0;
    float  pid_room_temp_ = 0, pid_target_room_ = 0;
    bool   pid_cycle_locked_ = false;
    int    pid_remaining_lockout_ = 0;
    bool   pid_ch_enabled_ = false;
    float  pid_kp_ = 2.0f, pid_ki_ = 0.01f, pid_kd_ = 0;
    int    pid_dt_sec_ = 60, pid_room_sensor_ = 0, pid_lockout_sec_ = 300;
    float  pid_hysteresis_ = 0.5f;
    CHMode ch_mode_ = CHMode::Manual_Static;
    bool   schedule_enabled_ = false;
    float  schedule_temps_[24] = {};
    bool   pid_schedule_enabled_ = false;
    float  pid_schedule_temps_[24] = {};
    float  dhw_hysteresis_ = 2.0f;
    int    tz_offset_ = 3;
    std::string sntp_srv0_, sntp_srv1_;
    float  k_calib_ = 1.0f, p_max_ = 24.0f, gas_calorific_ = 9.5f, gas_meter_base_ = 0;
    bool   mqtt_connected_ = false;
    float  gas_temp_offset_ = -5.0f;
    float  ch_pmin_ = 5.5f, ch_pmax_ = 24.0f;
    float  dhw_pmin_ = 5.5f, dhw_pmax_ = 24.0f;
    float  eff_t1_ = 30.0f, eff_v1_ = 0.98f;
    float  eff_t2_ = 55.0f, eff_v2_ = 0.93f;
    float  eff_t3_ = 80.0f, eff_v3_ = 0.88f;
};
