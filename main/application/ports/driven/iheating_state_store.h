#pragma once

#include <stdint.h>
#include "domain/value_objects/ch_mode.h"

/// Read-write-locked in-memory state store.
/// Single synchronization point for poll task (writer) and HTTP handlers (readers).
///
/// lock_shared() / unlock_shared()  — for readers (HTTP GET, NVS save)
/// lock_exclusive() / unlock_exclusive() — for writers (poll task, HTTP POST config)
class IHeatingStateStore {
public:
    // ── Lock protocol ──────────────────────────────────
    virtual void lock_shared() = 0;
    virtual void unlock_shared() = 0;
    virtual void lock_exclusive() = 0;
    virtual void unlock_exclusive() = 0;

    // ── Boiler status ──────────────────────────────────
    virtual void set_connected(bool v) = 0;
    virtual void set_fault(bool v) = 0;
    virtual void set_flame(bool v) = 0;
    virtual void set_ch_active(bool v) = 0;
    virtual void set_dhw_active(bool v) = 0;

    virtual bool is_connected()  const = 0;
    virtual bool has_fault()     const = 0;
    virtual bool is_flame_on()   const = 0;
    virtual bool is_ch_active()  const = 0;
    virtual bool is_dhw_active() const = 0;

    // ── Temperatures ───────────────────────────────────
    virtual void set_ch_temp(float v) = 0;
    virtual void set_dhw_temp(float v) = 0;
    virtual void set_return_temp(float v) = 0;
    virtual void set_outside_temp(float v) = 0;
    virtual void set_modulation(float v) = 0;
    virtual void set_t1_temp(float v) = 0;
    virtual void set_t2_temp(float v) = 0;

    virtual float get_ch_temp()     const = 0;
    virtual float get_dhw_temp()    const = 0;
    virtual float get_return_temp() const = 0;
    virtual float get_outside_temp() const = 0;
    virtual float get_modulation()  const = 0;
    virtual float get_t1_temp()     const = 0;
    virtual float get_t2_temp()     const = 0;

    // ── Setpoints & bounds ─────────────────────────────
    virtual void set_ch_setpoint(float v) = 0;
    virtual void set_dhw_setpoint(float v) = 0;
    virtual void set_ch_sp_min(float v) = 0;
    virtual void set_ch_sp_max(float v) = 0;
    virtual void set_dhw_sp_min(float v) = 0;
    virtual void set_dhw_sp_max(float v) = 0;

    virtual float get_ch_setpoint()  const = 0;
    virtual float get_dhw_setpoint() const = 0;
    virtual float get_ch_sp_min()    const = 0;
    virtual float get_ch_sp_max()    const = 0;
    virtual float get_dhw_sp_min()   const = 0;
    virtual float get_dhw_sp_max()   const = 0;

    // ── Enables ────────────────────────────────────────
    virtual void set_ch_enable(bool v) = 0;
    virtual void set_dhw_enable(bool v) = 0;
    virtual bool is_ch_enabled()  const = 0;
    virtual bool is_dhw_enabled() const = 0;

    // ── Fault codes ────────────────────────────────────
    virtual void set_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) = 0;
    virtual uint8_t  get_asf_flags()       const = 0;
    virtual uint8_t  get_oem_fault_code()  const = 0;
    virtual uint16_t get_oem_diagnostic()  const = 0;

    // ── Runtime counters ───────────────────────────────
    virtual void set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) = 0;
    virtual void set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) = 0;
    virtual uint16_t get_burner_starts()    const = 0;
    virtual uint16_t get_ch_pump_starts()   const = 0;
    virtual uint16_t get_dhw_valve_starts() const = 0;
    virtual uint16_t get_dhw_burner_starts() const = 0;
    virtual uint16_t get_burner_hours()     const = 0;
    virtual uint16_t get_ch_pump_hours()    const = 0;
    virtual uint16_t get_dhw_valve_hours()  const = 0;
    virtual uint16_t get_dhw_burner_hours() const = 0;

    // ── Version ────────────────────────────────────────
    virtual void set_version(uint8_t slave_type, uint8_t slave_ver, float ot_ver) = 0;
    virtual uint8_t get_slave_type()    const = 0;
    virtual uint8_t get_slave_version() const = 0;
    virtual float   get_ot_version()    const = 0;

    // ── DHW session ────────────────────────────────────
    virtual void set_dhw_session_finished(uint32_t dur_ms, float min_temp) = 0;
    virtual void set_dhw_prediction(bool active, int remaining_sec, int uncertainty_sec,
                                     float rate_cps, int elapsed_sec) = 0;
    virtual int  get_dhw_last_session_sec()      const = 0;
    virtual bool get_dhw_pred_active()           const = 0;
    virtual int  get_dhw_pred_remaining_sec()    const = 0;
    virtual int  get_dhw_pred_uncertainty_sec()  const = 0;
    virtual int   get_dhw_pred_elapsed_sec()      const = 0;
    virtual float get_dhw_pred_rate_cps()        const = 0;

    // ── PID state ──────────────────────────────────────
    virtual void set_pid_state(bool enabled, bool active, float output,
                                float p, float i, float d,
                                float room_temp, float target_room,
                                bool cycle_locked, int remaining_lockout,
                                bool ch_enabled_by_pid) = 0;
    virtual void set_pid_config(float kp, float ki, float kd, int dt_sec,
                                 int room_sensor, float target_room, int lockout_sec) = 0;
    virtual void set_pid_hysteresis(float v) = 0;

    virtual bool  get_pid_enabled()            const = 0;
    virtual bool  get_pid_active()             const = 0;
    virtual float get_pid_output()             const = 0;
    virtual float get_pid_p()                  const = 0;
    virtual float get_pid_i()                  const = 0;
    virtual float get_pid_d()                  const = 0;
    virtual float get_pid_room_temp()          const = 0;
    virtual float get_pid_target_room()        const = 0;
    virtual bool  get_pid_cycle_locked()       const = 0;
    virtual int   get_pid_remaining_lockout()  const = 0;
    virtual bool  get_pid_ch_enabled_by_pid()  const = 0;
    virtual float get_pid_kp()                 const = 0;
    virtual float get_pid_ki()                 const = 0;
    virtual float get_pid_kd()                 const = 0;
    virtual int   get_pid_dt_sec()             const = 0;
    virtual int   get_pid_room_sensor()        const = 0;
    virtual int   get_pid_lockout_sec()        const = 0;
    virtual float get_pid_hysteresis()         const = 0;

    // ── CH mode ────────────────────────────────────────
    virtual void set_ch_mode(CHMode v) = 0;
    virtual CHMode get_ch_mode() const = 0;

    // ── Schedule ───────────────────────────────────────
    virtual void set_schedule(const void* sched) = 0; // CH_Schedule*
    virtual void get_schedule(void* sched) const = 0;

    // ── PID Schedule ───────────────────────────────────
    virtual void set_pid_schedule(const void* sched) = 0; // PID_Schedule*
    virtual void get_pid_schedule(void* sched) const = 0;

    // ── Hysteresis ─────────────────────────────────────
    virtual void  set_dhw_hysteresis(float v) = 0;
    virtual float get_dhw_hysteresis() const = 0;

    // ── Timezone & SNTP ────────────────────────────────
    virtual void set_tz_offset(int v) = 0;
    virtual int  get_tz_offset() const = 0;
    virtual void set_sntp_server0(const char* v) = 0;
    virtual void set_sntp_server1(const char* v) = 0;
    virtual const char* get_sntp_server0() const = 0;
    virtual const char* get_sntp_server1() const = 0;

    // ── Gas / calibration (for web presenter) ──────────
    virtual void set_k_calib(float v) = 0;
    virtual float get_k_calib() const = 0;
    virtual void set_p_max(float v) = 0;
    virtual float get_p_max() const = 0;
    virtual void set_gas_calorific(float v) = 0;
    virtual float get_gas_calorific() const = 0;
    virtual void set_gas_meter_base(float v) = 0;
    virtual float get_gas_meter_base() const = 0;

    // ── MQTT status (для отображения в веб-интерфейсе) ──
    virtual void set_mqtt_connected(bool v) = 0;
    virtual bool is_mqtt_connected() const = 0;

    virtual ~IHeatingStateStore() = default;
};
