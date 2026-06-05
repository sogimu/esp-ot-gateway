#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <array>

typedef struct {
    bool  enabled;
    float temps[24];
} CH_Schedule;

#define CORRECTION_LOG_SIZE 32
#define LOG_RING_SIZE 512

struct CorrectionEntry {
    uint32_t timestamp;
    float    actual_reading;
    float    estimated_total;
    float    difference;
    float    prev_k_calib;
    float    new_k_calib;
};

enum LogCategory {
    LOG_CAT_SYSTEM,
    LOG_CAT_USER,
    LOG_CAT_EQUIP,
    LOG_CAT_MODE
};

struct LogEntry {
    uint32_t time_sec;
    uint8_t  category;
    char     msg[48];
};

struct StatsData {
    int   sample_count = 0;
    int   cycle_count  = 0;
    float median_burn   = 0;
    float median_pause  = 0;
    float avg_burn      = 0;
    float avg_pause     = 0;
    float burner_hours  = 0;
    float p1   = 0;
    float p10  = 0;
    float p25  = 0;
    float p50  = 0;
    float p75  = 0;
    float p90  = 0;
    float p99  = 0;
};

struct GasData {
    float instant_flow   = 0;  // м³/ч
    float integral_m3    = 0;  // накопленный объём, м³
    float avg_1h         = 0;  // средний за 1 час, м³/ч
    float avg_3h         = 0;  // средний за 3 часа, м³/ч
    float avg_12h        = 0;  // средний за 12 часов, м³/ч
    float avg_24h        = 0;  // средний за сутки, м³/ч
    float avg_7d         = 0;  // средний за неделю, м³/ч
    float mod_filtered   = 0;  // модуляция после фильтра Калмана, %
    float t_ret_filtered = 0;  // T_return после фильтра Калмана, °C
};

class Model {
public:
    Model();

    void set_connected(bool v);
    void set_fault(bool v);
    void set_flame(bool v);
    void set_ch_active(bool v);
    void set_dhw_active(bool v);
    void set_ch_temp(float v);
    void set_dhw_temp(float v);
    void set_return_temp(float v);
    void set_outside_temp(float v);
    void set_modulation(float v);
    void set_t1_temp(float v);
    void set_t2_temp(float v);

    void set_ch_setpoint(float v);
    void set_dhw_setpoint(float v);
    void set_ch_sp_min(float v);
    void set_ch_sp_max(float v);
    void set_dhw_sp_min(float v);
    void set_dhw_sp_max(float v);

    void set_ch_enable(bool v);
    void set_dhw_enable(bool v);

    void set_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag);
    void set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs);
    void set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh);
    void set_version(uint8_t slave_type, uint8_t slave_ver, float ot_ver);
    void set_dhw_session_finished(uint32_t dur_ms, float min_temp);
    void set_dhw_prediction(bool active, int remaining_sec, int uncertainty_sec, float rate_cps, int elapsed_sec);
    void set_pid_state(bool enabled, bool active, float output,
                       float p, float i, float d,
                       float room_temp, float target_room,
                       bool cycle_locked, int remaining_lockout);
    void set_pid_config(float kp, float ki, float kd, int dt_sec, int room_sensor, float target_room, int lockout_sec);
    void set_schedule(const CH_Schedule& sched);
    void set_tz_offset(int v);
    void set_dhw_hysteresis(float v);
    float get_dhw_hysteresis() const { return dhw_hyst_on_; }
    void set_sntp_server0(const char* v);
    const char* get_sntp_server0() const { return sntp_srv0_; }
    void set_sntp_server1(const char* v);
    const char* get_sntp_server1() const { return sntp_srv1_; }

    bool   is_connected()   const { return connected_; }
    bool   has_fault()      const { return fault_; }
    bool   is_flame_on()    const { return flame_; }
    bool   is_ch_active()   const { return ch_active_; }
    bool   is_dhw_active()  const { return dhw_active_; }
    float  get_ch_temp()    const { return ch_temp_; }
    float  get_dhw_temp()   const { return dhw_temp_; }
    float  get_return_temp()const { return return_temp_; }
    float  get_outside_temp()const{ return outside_temp_; }
    float  get_modulation() const { return modulation_; }
    float  get_t1_temp()    const { return t1_temp_; }
    float  get_t2_temp()    const { return t2_temp_; }
    float  get_ch_setpoint()const { return ch_setpoint_; }
    float  get_dhw_setpoint()const{ return dhw_setpoint_; }
    float  get_ch_sp_min()  const { return ch_sp_min_; }
    float  get_ch_sp_max()  const { return ch_sp_max_; }
    float  get_dhw_sp_min() const { return dhw_sp_min_; }
    float  get_dhw_sp_max() const { return dhw_sp_max_; }
    bool   is_ch_enabled()  const { return ch_enable_; }
    bool   is_dhw_enabled() const { return dhw_enable_; }
    uint8_t get_asf_flags()    const { return asf_flags_; }
    uint8_t get_oem_fault_code()const { return oem_fault_code_; }
    uint16_t get_oem_diagnostic() const { return oem_diagnostic_; }
    uint16_t get_burner_starts()   const { return burner_starts_; }
    uint16_t get_ch_pump_starts()  const { return ch_pump_starts_; }
    uint16_t get_dhw_valve_starts()const { return dhw_valve_starts_; }
    uint16_t get_dhw_burner_starts()const{ return dhw_burner_starts_; }
    uint16_t get_burner_hours()    const { return burner_hours_; }
    uint16_t get_ch_pump_hours()   const { return ch_pump_hours_; }
    uint16_t get_dhw_valve_hours() const { return dhw_valve_hours_; }
    uint16_t get_dhw_burner_hours()const{ return dhw_burner_hours_; }
    uint8_t  get_slave_type()  const { return slave_type_; }
    uint8_t  get_slave_version()const { return slave_version_; }
    float    get_ot_version()  const { return ot_version_; }
    int      get_dhw_last_session_sec() const { return dhw_last_session_sec_; }
    bool     get_dhw_pred_active() const { return dhw_pred_active_; }
    int      get_dhw_pred_remaining_sec() const { return dhw_pred_remaining_sec_; }
    int      get_dhw_pred_uncertainty_sec() const { return dhw_pred_uncertainty_sec_; }
    int      get_dhw_pred_elapsed_sec() const { return dhw_pred_elapsed_sec_; }
    bool     get_pid_enabled() const { return pid_enabled_; }
    bool     get_pid_controller_active() const { return pid_active_; }
    float    get_pid_output() const { return pid_output_; }
    float    get_pid_p() const { return pid_p_; }
    float    get_pid_i() const { return pid_i_; }
    float    get_pid_d() const { return pid_d_; }
    float    get_pid_room_temp() const { return pid_room_temp_; }
    float    get_pid_target_room() const { return pid_target_room_; }
    bool     get_pid_cycle_locked() const { return pid_cycle_locked_; }
    int      get_pid_remaining_lockout() const { return pid_remaining_lockout_; }
    float    get_pid_kp() const { return pid_kp_; }
    float    get_pid_ki() const { return pid_ki_; }
    float    get_pid_kd() const { return pid_kd_; }
    int      get_pid_dt_sec() const { return pid_dt_sec_; }
    int      get_pid_room_sensor() const { return pid_room_sensor_; }
    int      get_pid_lockout_sec() const { return pid_lockout_sec_; }
    const CH_Schedule& get_schedule() const { return schedule_; }
    int      get_tz_offset()   const { return tz_offset_; }

    void set_stats(const StatsData& s);
    const StatsData& get_stats() const;
    void set_gas_data(const GasData& d);
    const GasData& get_gas_data() const;
    void set_gas_meter_base(float v);
    float get_gas_meter_base() const;
    float get_gas_meter_total() const;
    float get_last_correction_actual() const;
    float get_integral_at_last_correction() const;
    void set_last_correction_refs(float actual, float integral);
    int  get_correction_count() const { return corrections_count_; }
    const CorrectionEntry* get_corrections() const { return corrections_.data(); }
    void add_correction(const CorrectionEntry& e);
    void get_correction_by_index(int idx, CorrectionEntry& out) const;
    void set_k_calib(float v);
    float get_k_calib() const;
    void set_p_max(float v);
    float get_p_max() const;
    void set_gas_calorific(float v);
    float get_gas_calorific() const;
    std::string to_stats_json() const;

    void add_log_entry(uint32_t time_sec, uint8_t cat, const char* msg);
    std::string to_log_json() const;
    int  get_log_count()   const { return log_count_; }
    int  get_log_head()    const { return log_head_; }
    const std::array<LogEntry, LOG_RING_SIZE>& get_log_ring() const { return log_ring_; }

    std::string to_json() const;

private:
    bool   connected_;
    bool   fault_, flame_, ch_active_, dhw_active_;
    float  ch_temp_, dhw_temp_, return_temp_, outside_temp_;
    float  modulation_;
    float  t1_temp_, t2_temp_;
    float  ch_setpoint_, dhw_setpoint_;
    float  ch_sp_min_, ch_sp_max_, dhw_sp_min_, dhw_sp_max_;
    bool   ch_enable_, dhw_enable_;
    uint8_t  asf_flags_, oem_fault_code_;
    uint16_t oem_diagnostic_;
    uint16_t burner_starts_, ch_pump_starts_, dhw_valve_starts_, dhw_burner_starts_;
    uint16_t burner_hours_, ch_pump_hours_, dhw_valve_hours_, dhw_burner_hours_;
    uint8_t  slave_type_, slave_version_;
    float    ot_version_;
    int      dhw_last_session_sec_;
    bool     dhw_pred_active_ = false;
    int      dhw_pred_remaining_sec_ = 0;
    int      dhw_pred_uncertainty_sec_ = 0;
    int      dhw_pred_elapsed_sec_ = 0;
    float    dhw_pred_rate_cps_ = 0;
    bool     pid_enabled_ = false;
    bool     pid_active_ = false;
    float    pid_output_ = 0;
    float    pid_p_ = 0;
    float    pid_i_ = 0;
    float    pid_d_ = 0;
    float    pid_room_temp_ = 0;
    float    pid_target_room_ = 0;
    bool     pid_cycle_locked_ = false;
    int      pid_remaining_lockout_ = 0;
    float    pid_kp_ = 2.0f;
    float    pid_ki_ = 0.01f;
    float    pid_kd_ = 0.0f;
    int      pid_dt_sec_ = 60;
    int      pid_room_sensor_ = 0;
    int      pid_lockout_sec_ = 300;
    CH_Schedule schedule_;
    int      tz_offset_;
    float    dhw_hyst_on_ = 2.0f;
    char     sntp_srv0_[64] = {};
    char     sntp_srv1_[64] = {};

    StatsData stats_;
    GasData   gas_data_;
    float k_calib_ = 1.0f;
    float p_max_ = 24.0f;
    float gas_calorific_ = 9.5f;

    float gas_meter_base_ = 0.0f;
    float last_correction_actual_ = 0.0f;
    float integral_at_last_correction_ = 0.0f;
    std::array<CorrectionEntry, CORRECTION_LOG_SIZE> corrections_;
    int corrections_head_ = 0;
    int corrections_count_ = 0;
    std::array<LogEntry, LOG_RING_SIZE> log_ring_;
    int log_head_ = 0;
    int log_count_ = 0;
};
