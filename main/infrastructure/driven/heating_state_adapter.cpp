#include "infrastructure/driven/heating_state_adapter.h"
#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"
#include <cstring>

// FreeRTOS read-write lock — prevents torn reads on arrays and multi-field structs
void HeatingStateAdapter::lock_shared()     { shared_mutex_lock_shared(&mutex_); }
void HeatingStateAdapter::unlock_shared()   { shared_mutex_unlock_shared(&mutex_); }
void HeatingStateAdapter::lock_exclusive()  { shared_mutex_lock_exclusive(&mutex_); }
void HeatingStateAdapter::unlock_exclusive(){ shared_mutex_unlock_exclusive(&mutex_); }

// Boiler status
void HeatingStateAdapter::set_connected(bool v) { state_.connected_ = v; }
void HeatingStateAdapter::set_fault(bool v) { state_.fault_ = v; }
void HeatingStateAdapter::set_flame(bool v) { state_.flame_ = v; }
void HeatingStateAdapter::set_ch_active(bool v) { state_.ch_active_ = v; }
void HeatingStateAdapter::set_dhw_active(bool v) { state_.dhw_active_ = v; }
bool HeatingStateAdapter::is_connected()  const { return state_.connected_; }
bool HeatingStateAdapter::has_fault()     const { return state_.fault_; }
bool HeatingStateAdapter::is_flame_on()   const { return state_.flame_; }
bool HeatingStateAdapter::is_ch_active()  const { return state_.ch_active_; }
bool HeatingStateAdapter::is_dhw_active() const { return state_.dhw_active_; }

// Temperatures
void HeatingStateAdapter::set_ch_temp(float v) { state_.ch_temp_ = v; }
void HeatingStateAdapter::set_dhw_temp(float v) { state_.dhw_temp_ = v; }
void HeatingStateAdapter::set_return_temp(float v) { state_.return_temp_ = v; }
void HeatingStateAdapter::set_outside_temp(float v) { state_.outside_temp_ = v; }
void HeatingStateAdapter::set_modulation(float v) { state_.modulation_ = v; }
void HeatingStateAdapter::set_t1_temp(float v) { state_.t1_temp_ = v; }
void HeatingStateAdapter::set_t2_temp(float v) { state_.t2_temp_ = v; }
float HeatingStateAdapter::get_ch_temp()     const { return state_.ch_temp_; }
float HeatingStateAdapter::get_dhw_temp()    const { return state_.dhw_temp_; }
float HeatingStateAdapter::get_return_temp() const { return state_.return_temp_; }
float HeatingStateAdapter::get_outside_temp() const { return state_.outside_temp_; }
float HeatingStateAdapter::get_modulation()  const { return state_.modulation_; }
float HeatingStateAdapter::get_t1_temp()     const { return state_.t1_temp_; }
float HeatingStateAdapter::get_t2_temp()     const { return state_.t2_temp_; }

// Setpoints & bounds
#define IMPL_GETSET_F(type, name) \
  void HeatingStateAdapter::set_##name(type v) { state_.name##_ = v; } \
  type HeatingStateAdapter::get_##name() const { return state_.name##_; }

IMPL_GETSET_F(float, ch_setpoint)
IMPL_GETSET_F(float, dhw_setpoint)
IMPL_GETSET_F(float, ch_sp_min)
IMPL_GETSET_F(float, ch_sp_max)
IMPL_GETSET_F(float, dhw_sp_min)
IMPL_GETSET_F(float, dhw_sp_max)
IMPL_GETSET_F(float, k_calib)
IMPL_GETSET_F(float, p_max)
IMPL_GETSET_F(float, gas_calorific)
IMPL_GETSET_F(float, gas_meter_base)
IMPL_GETSET_F(float, dhw_hysteresis)
IMPL_GETSET_F(float, gas_temp_offset)
IMPL_GETSET_F(float, ch_pmin_warm)
IMPL_GETSET_F(float, ch_pmax_warm)
IMPL_GETSET_F(float, ch_pmin_hot)
IMPL_GETSET_F(float, ch_pmax_hot)
IMPL_GETSET_F(float, dhw_pmin)
IMPL_GETSET_F(float, dhw_pmax)
IMPL_GETSET_F(float, eff_t1)
IMPL_GETSET_F(float, eff_v1)
IMPL_GETSET_F(float, eff_t2)
IMPL_GETSET_F(float, eff_v2)
IMPL_GETSET_F(float, eff_t3)
IMPL_GETSET_F(float, eff_v3)

#define IMPL_GETSET_B(type, name) \
  void HeatingStateAdapter::set_##name(type v) { state_.name##_ = v; } \
  type HeatingStateAdapter::is_##name() const { return state_.name##_; }

void HeatingStateAdapter::set_ch_enable(bool v) { state_.ch_enable_ = v; }
bool HeatingStateAdapter::is_ch_enabled()  const { return state_.ch_enable_; }
void HeatingStateAdapter::set_dhw_enable(bool v) { state_.dhw_enable_ = v; }
bool HeatingStateAdapter::is_dhw_enabled() const { return state_.dhw_enable_; }

#define IMPL_GETSET_I(type, name) \
  void HeatingStateAdapter::set_##name(type v) { state_.name##_ = v; } \
  type HeatingStateAdapter::get_##name() const { return state_.name##_; }

void HeatingStateAdapter::set_ch_mode(CHMode v) { state_.ch_mode_ = v; }
CHMode HeatingStateAdapter::get_ch_mode() const { return state_.ch_mode_; }
IMPL_GETSET_I(int, tz_offset)

// Fault codes
void HeatingStateAdapter::set_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) {
    state_.asf_flags_ = asf; state_.oem_fault_code_ = oem_fault; state_.oem_diagnostic_ = oem_diag;
}
uint8_t  HeatingStateAdapter::get_asf_flags()       const { return state_.asf_flags_; }
uint8_t  HeatingStateAdapter::get_oem_fault_code()  const { return state_.oem_fault_code_; }
uint16_t HeatingStateAdapter::get_oem_diagnostic()  const { return state_.oem_diagnostic_; }

// Runtime counters
void HeatingStateAdapter::set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) {
    state_.burner_starts_ = bs; state_.ch_pump_starts_ = cps;
    state_.dhw_valve_starts_ = dvs; state_.dhw_burner_starts_ = dbs;
}
void HeatingStateAdapter::set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) {
    state_.burner_hours_ = bh; state_.ch_pump_hours_ = cph;
    state_.dhw_valve_hours_ = dvh; state_.dhw_burner_hours_ = dbh;
}
#define IMPL_GET_U16(name) uint16_t HeatingStateAdapter::get_##name() const { return state_.name##_; }
IMPL_GET_U16(burner_starts)
IMPL_GET_U16(ch_pump_starts)
IMPL_GET_U16(dhw_valve_starts)
IMPL_GET_U16(dhw_burner_starts)
IMPL_GET_U16(burner_hours)
IMPL_GET_U16(ch_pump_hours)
IMPL_GET_U16(dhw_valve_hours)
IMPL_GET_U16(dhw_burner_hours)

// Version
void HeatingStateAdapter::set_version(uint8_t slave_type, uint8_t slave_ver, float ot_ver) {
    state_.slave_type_ = slave_type; state_.slave_version_ = slave_ver; state_.ot_version_ = ot_ver;
}
uint8_t HeatingStateAdapter::get_slave_type()    const { return state_.slave_type_; }
uint8_t HeatingStateAdapter::get_slave_version() const { return state_.slave_version_; }
float   HeatingStateAdapter::get_ot_version()    const { return state_.ot_version_; }

// DHW session
void HeatingStateAdapter::set_dhw_session_finished(uint32_t dur_ms, float /*min_temp*/) {
    state_.dhw_last_session_sec_ = (int)(dur_ms / 1000);
}
void HeatingStateAdapter::set_dhw_prediction(bool active, int remaining, int uncertainty, float rate, int elapsed) {
    state_.dhw_pred_active_ = active; state_.dhw_pred_remaining_sec_ = remaining;
    state_.dhw_pred_uncertainty_sec_ = uncertainty; state_.dhw_pred_rate_cps_ = rate;
    state_.dhw_pred_elapsed_sec_ = elapsed;
}
int  HeatingStateAdapter::get_dhw_last_session_sec()      const { return state_.dhw_last_session_sec_; }
bool HeatingStateAdapter::get_dhw_pred_active()           const { return state_.dhw_pred_active_; }
int  HeatingStateAdapter::get_dhw_pred_remaining_sec()    const { return state_.dhw_pred_remaining_sec_; }
int  HeatingStateAdapter::get_dhw_pred_uncertainty_sec()  const { return state_.dhw_pred_uncertainty_sec_; }
int   HeatingStateAdapter::get_dhw_pred_elapsed_sec()      const { return state_.dhw_pred_elapsed_sec_; }
float HeatingStateAdapter::get_dhw_pred_rate_cps()        const { return state_.dhw_pred_rate_cps_; }

// PID state
void HeatingStateAdapter::set_pid_state(bool en, bool act, float out,
                                         float p, float i, float d,
                                         float rt, float tr,
                                         bool cl, int rl, bool ce) {
    state_.pid_enabled_ = en; state_.pid_active_ = act; state_.pid_output_ = out;
    state_.pid_p_ = p; state_.pid_i_ = i; state_.pid_d_ = d;
    state_.pid_room_temp_ = rt; state_.pid_target_room_ = tr;
    state_.pid_cycle_locked_ = cl; state_.pid_remaining_lockout_ = rl;
    state_.pid_ch_enabled_by_pid_ = ce;
}
void HeatingStateAdapter::set_pid_config(float kp, float ki, float kd, int dt, int rs, float tr, int lo) {
    state_.pid_kp_ = kp; state_.pid_ki_ = ki; state_.pid_kd_ = kd;
    state_.pid_dt_sec_ = dt; state_.pid_room_sensor_ = rs;
    state_.pid_target_room_ = tr; state_.pid_lockout_sec_ = lo;
}
void HeatingStateAdapter::set_pid_hysteresis(float v) { state_.pid_hysteresis_ = v; }

#define IMPL_PID_GET(type, name) type HeatingStateAdapter::get_pid_##name() const { return state_.pid_##name##_; }
IMPL_PID_GET(bool, enabled)
IMPL_PID_GET(bool, active)
IMPL_PID_GET(float, output)
IMPL_PID_GET(float, p)
IMPL_PID_GET(float, i)
IMPL_PID_GET(float, d)
IMPL_PID_GET(float, room_temp)
IMPL_PID_GET(float, target_room)
IMPL_PID_GET(bool, cycle_locked)
IMPL_PID_GET(int, remaining_lockout)
IMPL_PID_GET(bool, ch_enabled_by_pid)
IMPL_PID_GET(float, kp)
IMPL_PID_GET(float, ki)
IMPL_PID_GET(float, kd)
IMPL_PID_GET(int, dt_sec)
IMPL_PID_GET(int, room_sensor)
IMPL_PID_GET(int, lockout_sec)
IMPL_PID_GET(float, hysteresis)

// Schedule
void HeatingStateAdapter::set_schedule(const void* sched) {
    const CH_Schedule& s = *static_cast<const CH_Schedule*>(sched);
    state_.schedule_enabled_ = s.enabled;
    memcpy(state_.schedule_temps_, s.temps, 24 * sizeof(float));
}
void HeatingStateAdapter::get_schedule(void* sched) const {
    CH_Schedule& s = *static_cast<CH_Schedule*>(sched);
    s.enabled = state_.schedule_enabled_;
    memcpy(s.temps, state_.schedule_temps_, 24 * sizeof(float));
}
void HeatingStateAdapter::set_pid_schedule(const void* sched) {
    const PID_Schedule& s = *static_cast<const PID_Schedule*>(sched);
    state_.pid_schedule_enabled_ = s.enabled;
    memcpy(state_.pid_schedule_temps_, s.temps, 24 * sizeof(float));
}
void HeatingStateAdapter::get_pid_schedule(void* sched) const {
    PID_Schedule& s = *static_cast<PID_Schedule*>(sched);
    s.enabled = state_.pid_schedule_enabled_;
    memcpy(s.temps, state_.pid_schedule_temps_, 24 * sizeof(float));
}

// SNTP
void HeatingStateAdapter::set_sntp_server0(const char* v) { strncpy(state_.sntp_srv0_, v, 63); }
void HeatingStateAdapter::set_sntp_server1(const char* v) { strncpy(state_.sntp_srv1_, v, 63); }
const char* HeatingStateAdapter::get_sntp_server0() const { return state_.sntp_srv0_; }
const char* HeatingStateAdapter::get_sntp_server1() const { return state_.sntp_srv1_; }
