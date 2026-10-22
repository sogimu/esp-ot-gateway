#include "application/use_cases/pid_poll_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/ilogger.h"
#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"
#include <cmath>
#include <cstdio>

static constexpr float  MAX_SAFE_ROOM     = 30.0f;

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

void PidPollInteractor::execute()
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
        float sp_min = pid_cfg_.out_min;
        state_.lock_exclusive();
        state_.set_pid_state(enabled, true, sp_min, 0, 0, 0, room, state_.get_pid_target_room(), cycle_locked_, 0, false);
        state_.set_ch_setpoint(sp_min);
        state_.unlock_exclusive();
        if (!overheat_logged_) {
            log_.event(ILogger::MODE, "PID: ПЕРЕГРЕВ %.1f>%.0f, SP %.0f", (double)room, (double)MAX_SAFE_ROOM, (double)sp_min);
            overheat_logged_ = true;
        }
        return;
    }
    overheat_logged_ = false;

    // Cycle locked — hold min setpoint
    if (cycle_locked_) {
        float sp_min = pid_cfg_.out_min;
        state_.lock_exclusive();
        state_.set_pid_state(enabled, true, sp_min, 0, 0, 0, room, state_.get_pid_target_room(), true, lockout, false);
        state_.set_ch_setpoint(sp_min);
        state_.unlock_exclusive();
        if (!lockout_logged_) {
            log_.event(ILogger::MODE, "PID: блокировка цикла, SP %.0f", (double)sp_min);
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

    float target = state_.get_pid_target_room();

    // Pure PI: the output IS the flow setpoint, bounded by pid_sp_min/pid_sp_max.
    compute_pid();

    float output = state_.get_pid_output();
    float sp_min = pid_cfg_.out_min;
    float deadband = state_.get_pid_hysteresis();
    if (deadband < 0.1f) deadband = 0.1f;  // a zero deadband would keep CH always on

    // CH on/off derived from the PI output (small deadband against chatter).
    bool ch_on;
    if (output >= sp_min + deadband) {
        ch_on = true;
    } else if (output <= sp_min) {
        ch_on = false;
    } else {
        ch_on = state_.get_pid_ch_enabled_by_pid();
    }

    state_.lock_exclusive();
    state_.set_pid_state(enabled, true, output,
                          state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                          room, target, false, 0, ch_on);
    // When CH is off, park the setpoint at the minimum.
    // CH enable bit is gated in BoilerPollInteractor::build_master_byte() by pid_ch_enabled.
    state_.set_ch_setpoint(ch_on ? output : sp_min);
    state_.unlock_exclusive();

    log_.event(ILogger::MODE,
        "PID: SP=%.0f°C цель=%.1f°C комн=%.1f°C CH=%s (P=%.1f I=%.1f)",
        (double)output, (double)target, (double)room,
        ch_on ? "вкл" : "выкл",
        (double)state_.get_pid_p(), (double)state_.get_pid_i());

    last_compute_ms_ = now;
}

void PidPollInteractor::load_config()
{
    // Effective flow setpoint bounds: user limits clamped by boiler limits (ID 49).
    float phys_min = (state_.get_ch_sp_min() >= 20.0f) ? state_.get_ch_sp_min() : 25.0f;
    float phys_max = (state_.get_ch_sp_max() >= 20.0f && state_.get_ch_sp_max() <= 80.0f)
                       ? state_.get_ch_sp_max() : 75.0f;
    float lo = state_.get_pid_sp_min();
    float hi = state_.get_pid_sp_max();
    if (lo < phys_min) lo = phys_min;
    if (lo > phys_max) lo = phys_max;
    if (hi < phys_min) hi = phys_min;
    if (hi > phys_max) hi = phys_max;
    if (hi < lo + 1.0f) hi = lo + 1.0f;

    if (!pid_inited_) {
        pid_cfg_.kp = state_.get_pid_kp();
        pid_cfg_.ki = state_.get_pid_ki();
        pid_cfg_.kd = state_.get_pid_kd();
        pid_init(&pid_state_);
        pid_state_.integral = lo;   // start at the minimum flow setpoint (no dead zone)
        pid_inited_ = true;
    }
    pid_cfg_.out_min = lo;
    pid_cfg_.out_max = hi;
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

    // Freeze integral during DHW; otherwise integrate freely — it also unwinds
    // when the room is above target, even while CH is off.
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
                "PID: расч.%.1f вне [%.0f..%.0f]°C (цель=%.1f комн=%.1f Δ=%.1f Kp=%.1f P=%.1f I=%.1f dt=%d)",
                (double)raw, (double)pid_cfg_.out_min, (double)pid_cfg_.out_max,
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
    float d_term = pid_state_.d_filt;

    state_.lock_exclusive();
    state_.set_pid_state(state_.get_pid_enabled(), true, out,
                          p_term, i_term, d_term,
                          room, target,
                          state_.get_pid_cycle_locked(),
                          state_.get_pid_remaining_lockout(),
                          state_.get_pid_ch_enabled_by_pid());
    state_.unlock_exclusive();
}
