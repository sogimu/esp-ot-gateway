#pragma once

#include "model/model.h"
#include "endpoints/opentherm/opentherm_endpoint.h"
#include "endpoints/sensors/sensors_endpoint.h"
#include "interfaces/iopentherm_observer.h"
#include "interfaces/isensors_observer.h"
#include "pid/pid.h"

#include "esp_timer.h"
#include "freertos/semphr.h"

class PidService : public IOpenthermObserver, public ISensorsObserver {
public:
    PidService(Model& model, OpenthermEndpoint& ot, SensorsEndpoint& sensors);
    ~PidService();

    void start();
    void stop();

    void set_enabled(bool en);
    void set_config(float kp, float ki, float kd, int dt_sec,
                    int room_sensor, float target_room, int lockout_sec);
    void set_hysteresis(float h);

    bool  is_enabled() const { return enabled_; }
    float get_kp()     const { return kp_; }
    float get_ki()     const { return ki_; }
    float get_kd()     const { return kd_; }
    int   get_dt_sec() const { return dt_sec_; }
    int   get_room_sensor() const { return room_sensor_; }
    float get_target_room() const { return target_room_; }
    int   get_lockout_sec() const { return lockout_sec_; }
    float get_hysteresis()  const { return hysteresis_; }

private:
    Model& model_;
    OpenthermEndpoint& ot_;
    SensorsEndpoint& sensors_;

    bool   enabled_ = false;
    float  kp_ = 2.0f, ki_ = 0.01f, kd_ = 0.0f;
    int    dt_sec_ = 60;
    int    room_sensor_ = 0;
    float  target_room_ = 22.0f;
    int    lockout_sec_ = 300;
    float  hysteresis_ = 0.5f;

    bool   active_ = false;
    bool   ch_enabled_by_pid_ = false;
    float  output_ = 0.0f;
    float  p_ = 0, i_ = 0, d_ = 0;

    float  room_temp_ = -127.0f;
    uint32_t last_room_temp_ms_ = 0;

    uint32_t last_flame_off_ms_ = 0;
    bool     prev_flame_ = false;
    bool     dhw_active_ = false;
    bool     cycle_locked_ = false;
    int      remaining_lockout_ = 0;
    bool     lockout_logged_ = false;
    bool     lockout_ended_ = false;
    bool     timeout_logged_ = false;
    bool     overheat_logged_ = false;
    bool     hyst_block_logged_ = false;

    uint32_t last_compute_ms_ = 0;
    uint32_t start_ms_ = 0;
    int      tick_count_ = 0;
    bool     sensor_timeout_ = false;

    PidConfig pid_cfg_;
    PidState  pid_state_;

    esp_timer_handle_t tick_timer_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;

    static void on_timer_static(void* arg);
    void on_tick();

    void push_to_model();

    bool is_flame_on() const { return prev_flame_; }

    void on_connected() override {}
    void on_disconnected() override {}
    void on_status_changed(bool fault, bool flame, bool ch_active, bool dhw_active) override;
    void on_ch_temp(float value) override { (void)value; }
    void on_dhw_temp(float value) override { (void)value; }
    void on_return_temp(float value) override { (void)value; }
    void on_outside_temp(float value) override { (void)value; }
    void on_modulation(float pct) override { (void)pct; }
    void on_ch_bounds(float min, float max) override { (void)min; (void)max; }
    void on_dhw_bounds(float min, float max) override { (void)min; (void)max; }
    void on_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) override { (void)asf; (void)oem_fault; (void)oem_diag; }
    void on_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) override { (void)bs; (void)cps; (void)dvs; (void)dbs; }
    void on_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) override { (void)bh; (void)cph; (void)dvh; (void)dbh; }
    void on_version(uint8_t st, uint8_t sv, float ov) override { (void)st; (void)sv; (void)ov; }
    void on_dhw_session_finished(uint32_t dur_ms, float min_temp) override { (void)dur_ms; (void)min_temp; }
    void on_dhw_session_started(float start_temp) override { (void)start_temp; }
    void on_ch_setpoint_confirmed(float value) override { (void)value; }
    void on_dhw_setpoint_confirmed(float value) override { (void)value; }

    void on_sensor_data(int sensor_id, float temperature) override;
};