#pragma once

#include "application/ports/driven/iheating_state_store.h"

extern "C" {
#include "infrastructure/freertos/shared_mutex.h"
}

/// In-memory state store with FreeRTOS read-write lock.
/// Poller task (writer) uses exclusive lock, HTTP server (reader) uses shared lock.
/// Prevents torn reads on arrays (schedule_temps_, sntp_srv*_) and multi-field structs.
class HeatingStateAdapter : public IHeatingStateStore {
public:
    HeatingStateAdapter() { shared_mutex_init(&mutex_); }

    // Lock protocol
    void lock_shared() override;
    void unlock_shared() override;
    void lock_exclusive() override;
    void unlock_exclusive() override;

    // Boiler status
    void set_connected(bool v) override;
    void set_fault(bool v) override;
    void set_flame(bool v) override;
    void set_ch_active(bool v) override;
    void set_dhw_active(bool v) override;
    bool is_connected()  const override;
    bool has_fault()     const override;
    bool is_flame_on()   const override;
    bool is_ch_active()  const override;
    bool is_dhw_active() const override;

    // Temperatures
    void set_ch_temp(float v) override;
    void set_dhw_temp(float v) override;
    void set_return_temp(float v) override;
    void set_outside_temp(float v) override;
    void set_modulation(float v) override;
    void set_t1_temp(float v) override;
    void set_t2_temp(float v) override;
    float get_ch_temp()     const override;
    float get_dhw_temp()    const override;
    float get_return_temp() const override;
    float get_outside_temp() const override;
    float get_modulation()  const override;
    float get_t1_temp()     const override;
    float get_t2_temp()     const override;

    // Setpoints & bounds
    void set_ch_setpoint(float v) override;
    void set_dhw_setpoint(float v) override;
    void set_ch_sp_min(float v) override;
    void set_ch_sp_max(float v) override;
    void set_dhw_sp_min(float v) override;
    void set_dhw_sp_max(float v) override;
    float get_ch_setpoint()  const override;
    float get_dhw_setpoint() const override;
    float get_ch_sp_min()    const override;
    float get_ch_sp_max()    const override;
    float get_dhw_sp_min()   const override;
    float get_dhw_sp_max()   const override;

    // Enables
    void set_ch_enable(bool v) override;
    void set_dhw_enable(bool v) override;
    bool is_ch_enabled()  const override;
    bool is_dhw_enabled() const override;

    // Fault codes
    void set_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) override;
    uint8_t  get_asf_flags()       const override;
    uint8_t  get_oem_fault_code()  const override;
    uint16_t get_oem_diagnostic()  const override;

    // Runtime counters
    void set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) override;
    void set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) override;
    uint16_t get_burner_starts()    const override;
    uint16_t get_ch_pump_starts()   const override;
    uint16_t get_dhw_valve_starts() const override;
    uint16_t get_dhw_burner_starts() const override;
    uint16_t get_burner_hours()     const override;
    uint16_t get_ch_pump_hours()    const override;
    uint16_t get_dhw_valve_hours()  const override;
    uint16_t get_dhw_burner_hours() const override;

    // Version
    void set_version(uint8_t slave_type, uint8_t slave_ver, float ot_ver) override;
    uint8_t get_slave_type()    const override;
    uint8_t get_slave_version() const override;
    float   get_ot_version()    const override;

    // DHW session
    void set_dhw_session_finished(uint32_t dur_ms, float min_temp) override;
    void set_dhw_prediction(bool active, int remaining_sec, int uncertainty_sec,
                             float rate_cps, int elapsed_sec) override;
    int  get_dhw_last_session_sec()      const override;
    bool get_dhw_pred_active()           const override;
    int  get_dhw_pred_remaining_sec()    const override;
    int  get_dhw_pred_uncertainty_sec()  const override;
    int   get_dhw_pred_elapsed_sec()      const override;
    float get_dhw_pred_rate_cps()        const override;

    // PID state
    void set_pid_state(bool enabled, bool active, float output,
                        float p, float i, float d,
                        float room_temp, float target_room,
                        bool cycle_locked, int remaining_lockout,
                        bool ch_enabled_by_pid) override;
    void set_pid_config(float kp, float ki, float kd, int dt_sec,
                         int room_sensor, float target_room, int lockout_sec) override;
    void set_pid_hysteresis(float v) override;
    bool  get_pid_enabled()            const override;
    bool  get_pid_active()             const override;
    float get_pid_output()             const override;
    float get_pid_p()                  const override;
    float get_pid_i()                  const override;
    float get_pid_d()                  const override;
    float get_pid_room_temp()          const override;
    float get_pid_target_room()        const override;
    bool  get_pid_cycle_locked()       const override;
    int   get_pid_remaining_lockout()  const override;
    bool  get_pid_ch_enabled_by_pid()  const override;
    float get_pid_kp()                 const override;
    float get_pid_ki()                 const override;
    float get_pid_kd()                 const override;
    int   get_pid_dt_sec()             const override;
    int   get_pid_room_sensor()        const override;
    int   get_pid_lockout_sec()        const override;
    float get_pid_hysteresis()         const override;

    // CH mode
    void set_ch_mode(CHMode v) override;
    CHMode get_ch_mode() const override;

    // Schedule (opaque implementation — casts to CH_Schedule in .cpp)
    void set_schedule(const void* sched) override;
    void get_schedule(void* sched) const override;

    // PID Schedule
    void set_pid_schedule(const void* sched) override;
    void get_pid_schedule(void* sched) const override;

    // Hysteresis
    void  set_dhw_hysteresis(float v) override;
    float get_dhw_hysteresis() const override;

    // Timezone & SNTP
    void set_tz_offset(int v) override;
    int  get_tz_offset() const override;
    void set_sntp_server0(const char* v) override;
    void set_sntp_server1(const char* v) override;
    const char* get_sntp_server0() const override;
    const char* get_sntp_server1() const override;

    // Gas / calibration
    void set_k_calib(float v) override;
    float get_k_calib() const override;
    void set_p_max(float v) override;
    float get_p_max() const override;
    void set_gas_calorific(float v) override;
    float get_gas_calorific() const override;
    void set_gas_meter_base(float v) override;
    float get_gas_meter_base() const override;

    // ── MQTT status ───────────────────────────────────
    void set_mqtt_connected(bool v) override;
    bool is_mqtt_connected() const override;

private:
    SharedMutex mutex_;

    struct State {
        bool   connected_ = false;
        bool   fault_ = false, flame_ = false, ch_active_ = false, dhw_active_ = false;
        float  ch_temp_ = 0, dhw_temp_ = 0, return_temp_ = 0, outside_temp_ = 0;
        float  modulation_ = 0;
        float  t1_temp_ = -127.0f, t2_temp_ = -127.0f;
        float  ch_setpoint_ = 30.0f, dhw_setpoint_ = 55.0f;
        float  ch_sp_min_ = 0, ch_sp_max_ = 80;
        float  dhw_sp_min_ = 0, dhw_sp_max_ = 65;
        bool   ch_enable_ = false, dhw_enable_ = false;
        uint8_t  asf_flags_ = 0, oem_fault_code_ = 0;
        uint16_t oem_diagnostic_ = 0;
        uint16_t burner_starts_ = 0, ch_pump_starts_ = 0, dhw_valve_starts_ = 0, dhw_burner_starts_ = 0;
        uint16_t burner_hours_ = 0, ch_pump_hours_ = 0, dhw_valve_hours_ = 0, dhw_burner_hours_ = 0;
        uint8_t  slave_type_ = 0, slave_version_ = 0;
        float    ot_version_ = 0;
        int      dhw_last_session_sec_ = 0;
        bool     dhw_pred_active_ = false;
        int      dhw_pred_remaining_sec_ = 0, dhw_pred_uncertainty_sec_ = 0, dhw_pred_elapsed_sec_ = 0;
        float    dhw_pred_rate_cps_ = 0;
        // PID
        bool   pid_enabled_ = false, pid_active_ = false;
        float  pid_output_ = 0, pid_p_ = 0, pid_i_ = 0, pid_d_ = 0;
        float  pid_room_temp_ = 0, pid_target_room_ = 0;
        bool   pid_cycle_locked_ = false;
        int    pid_remaining_lockout_ = 0;
        bool   pid_ch_enabled_by_pid_ = false;
        float  pid_kp_ = 2.0f, pid_ki_ = 0.01f, pid_kd_ = 0;
        int    pid_dt_sec_ = 60, pid_room_sensor_ = 0, pid_lockout_sec_ = 300;
        float  pid_hysteresis_ = 0.5f;
        CHMode ch_mode_ = CHMode::Manual_Static;
        // Schedule
        bool  schedule_enabled_ = false;
        float schedule_temps_[24] = {};
        // PID Schedule
        bool  pid_schedule_enabled_ = false;

        bool  mqtt_connected_ = false;
        float pid_schedule_temps_[24] = {};
        // Hysteresis
        float dhw_hysteresis_ = 2.0f;
        // Timezone
        int   tz_offset_ = 3;
        // SNTP
        char  sntp_srv0_[64] = {};
        char  sntp_srv1_[64] = {};
        // Gas
        float k_calib_ = 1.0f, p_max_ = 24.0f, gas_calorific_ = 9.5f;
        float gas_meter_base_ = 0;
    } state_;
};
