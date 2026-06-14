#include "application/use_cases/pid_poll_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/ilogger.h"
#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"
#include <cmath>
#include <cstdio>

static constexpr int    SENSOR_TIMEOUT_MS = 120000;
static constexpr float  MAX_SAFE_ROOM     = 30.0f;
static constexpr float  FALLBACK_SP       = 40.0f;
static constexpr float  MIN_SETPOINT      = 25.0f;

PidPollInteractor::PidPollInteractor(IHeatingStateStore& state, IBoilerHardware& boiler,
                                       ITimeSource& time, ILogger& log)
    : state_(state), boiler_(boiler), time_(time), log_(log)
{
    if (is_pid_mode(state_.get_ch_mode()))
        enable();
    else
        disable();
}

void PidPollInteractor::enable()
{
    bool was_enabled = state_.get_pid_enabled();
    prev_flame_ = false;
    cycle_locked_ = false;
    lockout_logged_ = false;
    overheat_logged_ = false;
    clamped_logged_ = false;
    last_flame_off_ms_ = 0;
    pid_inited_ = false;
    state_.lock_exclusive();
    state_.set_pid_state(true, state_.get_pid_active(), state_.get_pid_output(),
                          state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                          state_.get_pid_room_temp(), state_.get_pid_target_room(),
                          state_.get_pid_cycle_locked(), state_.get_pid_remaining_lockout(),
                          state_.get_pid_ch_enabled_by_pid());
    state_.unlock_exclusive();
    if (!was_enabled)
        log_.event(ILogger::MODE, "PID включён");
}

void PidPollInteractor::disable()
{
    bool was_enabled = state_.get_pid_enabled();
    prev_flame_ = false;
    cycle_locked_ = false;
    lockout_logged_ = false;
    overheat_logged_ = false;
    clamped_logged_ = false;
    last_flame_off_ms_ = 0;
    pid_inited_ = false;
    state_.lock_exclusive();
    state_.set_pid_state(false, false, 0, 0, 0, 0, 0, 0, false, 0, false);
    state_.unlock_exclusive();
    if (was_enabled)
        log_.event(ILogger::MODE, "PID выключен");
}

void PidPollInteractor::poll()
{
    uint32_t now = static_cast<uint32_t>(time_.monotonic_ms());
    if (last_compute_ms_ == 0) last_compute_ms_ = now;

    // Load latest config from state store
    load_config();

    bool enabled = state_.get_pid_enabled();
    if (!enabled) {
        state_.lock_exclusive();
        state_.set_pid_state(false, false, 0, 0, 0, 0, 0, 0, false, 0, false);
        state_.unlock_exclusive();
        return;
    }

    // If PID+schedule mode, update room target from schedule
    if (state_.get_ch_mode() == CHMode::PID_Sched) {
        PID_Schedule ps;
        state_.get_pid_schedule(&ps);
        if (ps.enabled) {
            auto hours = std::chrono::duration_cast<std::chrono::hours>(
                time_.local_now().time_since_epoch());
            int local_hour = hours.count() % 24;
            float sched_target = ps.get_for_hour(local_hour);
            state_.lock_exclusive();
            state_.set_pid_config(state_.get_pid_kp(), state_.get_pid_ki(), state_.get_pid_kd(),
                                  state_.get_pid_dt_sec(), state_.get_pid_room_sensor(),
                                  sched_target, state_.get_pid_lockout_sec());
            state_.unlock_exclusive();
        }
    }

    // Read room temp — from sensor 0 (T1) or 1 (T2) depending on config
    int sensor_id = state_.get_pid_room_sensor();
    state_.lock_shared();
    float room = (sensor_id == 0) ? state_.get_t1_temp() : state_.get_t2_temp();
    bool flame = state_.is_flame_on();
    state_.unlock_shared();

    bool room_valid = (room > -100.0f);

    // Flame tracking for cycle lockout
    if (flame != prev_flame_) {
        if (prev_flame_ && !flame) {
            last_flame_off_ms_ = now;
            cycle_locked_ = true;
        }
        prev_flame_ = flame;
    }

    // Cycle lockout countdown
    int lockout = state_.get_pid_lockout_sec();
    if (prev_flame_) {
        cycle_locked_ = false;
    } else if (last_flame_off_ms_ > 0 && !prev_flame_) {
        int elapsed = static_cast<int>((now - last_flame_off_ms_) / 1000);
        cycle_locked_ = (elapsed < lockout);
    }

    // Overheat protection — update state only; BoilerPollInteractor writes OT
    if (room_valid && room > MAX_SAFE_ROOM) {
        state_.lock_exclusive();
        state_.set_pid_state(enabled, true, MIN_SETPOINT, 0, 0, 0, room, state_.get_pid_target_room(), cycle_locked_, 0, false);
        state_.set_ch_setpoint(MIN_SETPOINT);  // signal BoilerPollInteractor via state
        state_.unlock_exclusive();
        if (!overheat_logged_) {
            log_.event(ILogger::MODE, "PID: ПЕРЕГРЕВ %.1f>%.0f, SP %.0f", (double)room, (double)MAX_SAFE_ROOM, (double)MIN_SETPOINT);
            overheat_logged_ = true;
        }
        return;
    }
    overheat_logged_ = false;

    // Cycle locked — hold min setpoint
    if (cycle_locked_) {
        state_.lock_exclusive();
        state_.set_pid_state(enabled, true, MIN_SETPOINT, 0, 0, 0, room, state_.get_pid_target_room(), true, lockout, false);
        state_.unlock_exclusive();
        if (!lockout_logged_) {
            log_.event(ILogger::MODE, "PID: блокировка цикла, SP %.0f", (double)MIN_SETPOINT);
            lockout_logged_ = true;
        }
        return;
    }
    lockout_logged_ = false;

    // Check dt interval
    int dt = state_.get_pid_dt_sec();
    uint32_t elapsed = now - last_compute_ms_;
    if (elapsed < static_cast<uint32_t>(dt * 1000)) {
        return; // not time to compute yet
    }

    if (!room_valid) return;

    // Compute PID
    compute_pid();

    float output = state_.get_pid_output();
    float target = state_.get_pid_target_room();
    float hysteresis = state_.get_pid_hysteresis();

    // Hysteresis for CH enable
    bool ch_on;
    if (!state_.is_ch_enabled() || room > target + hysteresis) {
        ch_on = false;
    } else if (room < target - hysteresis) {
        ch_on = true;
    } else {
        ch_on = state_.get_pid_ch_enabled_by_pid();
    }

    state_.lock_exclusive();
    state_.set_pid_state(enabled, true, output,
                          state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                          room, target, false, 0, ch_on);
    state_.unlock_exclusive();

    // State updated above; BoilerPollInteractor handles OT writes
    // CH setpoint written via state_.set_ch_setpoint(); CH enable via state_.set_ch_enable()
    if (ch_on) {
        state_.lock_exclusive();
        state_.set_ch_setpoint(output);
        state_.set_ch_enable(true);
        state_.unlock_exclusive();
    }

    log_.event(ILogger::MODE,
        "PID: SP=%.0f°C цель=%.1f°C комн=%.1f°C CH=%s (P=%.1f I=%.1f)",
        (double)output, (double)target, (double)room,
        ch_on ? "вкл" : "выкл",
        (double)state_.get_pid_p(), (double)state_.get_pid_i());

    last_compute_ms_ = now;
}

void PidPollInteractor::load_config()
{
    if (!pid_inited_) {
        pid_cfg_.kp = state_.get_pid_kp();
        pid_cfg_.ki = state_.get_pid_ki();
        pid_cfg_.kd = state_.get_pid_kd();
        pid_cfg_.out_min = (state_.get_ch_sp_min() >= 20.0f) ? state_.get_ch_sp_min() : 25.0f;
        pid_cfg_.out_max = (state_.get_ch_sp_max() >= 20.0f && state_.get_ch_sp_max() <= 80.0f)
                            ? state_.get_ch_sp_max() : 75.0f;
        pid_init(&pid_state_);
        pid_inited_ = true;
    }
    // Update bounds dynamically
    pid_cfg_.out_min = (state_.get_ch_sp_min() >= 20.0f) ? state_.get_ch_sp_min() : 25.0f;
    pid_cfg_.out_max = (state_.get_ch_sp_max() >= 20.0f && state_.get_ch_sp_max() <= 80.0f)
                        ? state_.get_ch_sp_max() : 75.0f;
}

void PidPollInteractor::compute_pid()
{
    int sensor_id = state_.get_pid_room_sensor();
    state_.lock_shared();
    float room = (sensor_id == 0) ? state_.get_t1_temp() : state_.get_t2_temp();
    bool dhw = state_.is_dhw_active();
    state_.unlock_shared();

    float target = state_.get_pid_target_room();
    int dt = state_.get_pid_dt_sec();
    if (dt < 1) dt = 60;

    // Freeze integral during DHW
    float saved_ki = pid_cfg_.ki;
    if (dhw) pid_cfg_.ki = 0.0f;

    float out = pid_step(&pid_cfg_, &pid_state_, target, room, dt);
    pid_cfg_.ki = saved_ki;

    if (out <= pid_cfg_.out_min || out >= pid_cfg_.out_max) {
        if (!clamped_logged_) {
            float error = target - room;
            float p_term = pid_cfg_.kp * error;
            float raw = p_term + pid_state_.integral + pid_state_.d_filt;
            log_.event(ILogger::MODE,
                "PID: расч.%.1f < мин.%.0f°C (цель=%.1f комн=%.1f Δ=%.1f Kp=%.1f P=%.1f I=%.1f dt=%d)",
                (double)raw, (double)pid_cfg_.out_min,
                (double)target, (double)room, (double)error,
                (double)pid_cfg_.kp, (double)p_term, (double)pid_state_.integral,
                dt);
            clamped_logged_ = true;
        }
    } else {
        clamped_logged_ = false;
    }

    float p_term = pid_cfg_.kp * (target - room);
    float i_term = pid_state_.integral;
    float d_term = 0; // simplified — the old code tracks d_filt

    state_.lock_exclusive();
    state_.set_pid_state(state_.get_pid_enabled(), true, out,
                          p_term, i_term, d_term,
                          room, target,
                          state_.get_pid_cycle_locked(),
                          state_.get_pid_remaining_lockout(),
                          state_.get_pid_ch_enabled_by_pid());
    state_.unlock_exclusive();
}
