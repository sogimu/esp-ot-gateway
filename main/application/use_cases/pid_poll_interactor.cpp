#include "application/use_cases/pid_poll_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itemperatures_source.h"
#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"
#include <cstdio>

static constexpr float  MAX_SAFE_ROOM     = 30.0f;
static constexpr float  UNWIND_GAIN       = 3.0f;   // faster integral unwind when the room is above target
static constexpr float  TRIM_MAX          = 5.0f;   // PI correction authority around the base
static constexpr float  WC_OUT_COLD       = -20.0f; // heating curve: cold outside point
static constexpr float  WC_OUT_WARM       = 15.0f;  // heating curve: warm outside point
static constexpr float  WC_FALLBACK       = 26.0f;  // base when the outside temperature is invalid

PidPollInteractor::PidPollInteractor(IHeatingStateStore& state, IBoilerHardware& boiler,
                                       ITimeSource& time, ILogger& log, ITemperatureSource* temps)
    : state_(state), boiler_(boiler), time_(time), log_(log), temps_(temps)
{
    if (is_pid_mode(state_.get_ch_mode()))
        enable();
    else
        disable();
}

void PidPollInteractor::enable()
{
    bool was_enabled = state_.get_pid_enabled();
    prev_ch_state_ = false;
    cycle_locked_ = false;
    lockout_logged_ = false;
    overheat_logged_ = false;
    clamped_logged_ = false;
    last_ch_off_ms_ = 0;
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
    prev_ch_state_ = false;
    cycle_locked_ = false;
    lockout_logged_ = false;
    overheat_logged_ = false;
    clamped_logged_ = false;
    last_ch_off_ms_ = 0;
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
    float room = (sensor_id == 0)
        ? (temps_ ? temps_->t1() : state_.get_t1_temp())
        : (temps_ ? temps_->t2() : state_.get_t2_temp());

    bool room_valid = (room > -100.0f);

    // Cycle lockout — armed by the PI turning CH off, not by the boiler flame.
    // The boiler's own modulation cycling must not pause CH.
    int lockout = state_.get_pid_lockout_sec();
    bool ch_prev = state_.get_pid_ch_enabled_by_pid();
    if (prev_ch_state_ && !ch_prev) {
        last_ch_off_ms_ = now;   // the PI has just turned CH off
    }
    prev_ch_state_ = ch_prev;
    if (lockout <= 0 || ch_prev) {
        cycle_locked_ = false;
    } else if (last_ch_off_ms_ > 0) {
        cycle_locked_ = ((now - last_ch_off_ms_) < static_cast<uint32_t>(lockout) * 1000);
    } else {
        cycle_locked_ = false;
    }

    // Keep the displayed lockout flag in sync when it expires before the next tick.
    if (state_.get_pid_cycle_locked() && !cycle_locked_) {
        state_.lock_exclusive();
        state_.set_pid_state(enabled, true, state_.get_pid_output(),
                             state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                             room, state_.get_pid_target_room(), false, 0,
                             state_.get_pid_ch_enabled_by_pid());
        state_.unlock_exclusive();
    }

    // Overheat protection — update state only; BoilerPollInteractor writes OT
    if (room_valid && room > MAX_SAFE_ROOM) {
        float sp_min = sp_lo_;
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
        float sp_min = sp_lo_;
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

    // Per-cycle bumpless: when CH was off, the trim starts fresh; the weather
    // base keeps the operating point.
    if (!ch_prev) pid_state_.integral = 0.0f;

    // Feed-forward base + bounded PI trim, using the actual elapsed time.
    int elapsed_sec = static_cast<int>(elapsed / 1000);
    if (elapsed_sec < 1) elapsed_sec = 1;
    if (elapsed_sec > 300) elapsed_sec = 300;
    compute_pid(elapsed_sec);

    float output = state_.get_pid_output();

    // CH on/off is a room thermostat with hysteresis; the PI keeps computing
    // the flow setpoint, and CH itself is gated by the enable bit.
    float hyst = state_.get_pid_hysteresis();
    if (hyst < 0.1f) hyst = 0.1f;
    bool ch_on;
    if (room > target + hyst) {
        ch_on = false;
    } else if (room < target - hyst) {
        ch_on = true;
    } else {
        ch_on = state_.get_pid_ch_enabled_by_pid();
    }

    state_.lock_exclusive();
    state_.set_pid_state(enabled, true, output,
                          state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                          room, target, false, 0, ch_on);
    // Keep the computed setpoint even when CH is off: the CH enable bit (gated
    // in BoilerPollInteractor by pid_ch_enabled) already stops heating, and the
    // setpoint is ready for a bumpless restart.
    state_.set_ch_setpoint(output);
    state_.unlock_exclusive();

    log_.event(ILogger::MODE,
        "PID: SP=%.0f°C цель=%.1f°C комн=%.1f°C CH=%s (P=%.1f I=%.1f база=%.1f)",
        (double)output, (double)target, (double)room,
        ch_on ? "вкл" : "выкл",
        (double)state_.get_pid_p(), (double)state_.get_pid_i(), (double)base_);

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
    sp_lo_ = lo;
    sp_hi_ = hi;

    // Reload gains every tick so runtime changes from the web/API apply at once.
    pid_cfg_.kp = state_.get_pid_kp();
    pid_cfg_.ki = state_.get_pid_ki();
    pid_cfg_.kd = state_.get_pid_kd();

    if (!pid_inited_) {
        pid_init(&pid_state_);
        pid_state_.integral = 0.0f;   // trim starts fresh; the base comes from the weather curve
        pid_inited_ = true;
    }
}

void PidPollInteractor::compute_pid(int dt_sec)
{
    int sensor_id = state_.get_pid_room_sensor();
    state_.lock_shared();
    bool dhw = state_.is_dhw_active();
    state_.unlock_shared();
    float room = (sensor_id == 0)
        ? (temps_ ? temps_->t1() : state_.get_t1_temp())
        : (temps_ ? temps_->t2() : state_.get_t2_temp());

    float target = state_.get_pid_target_room();
    int dt = dt_sec;
    if (dt < 1) dt = 1;
    if (dt > 300) dt = 300;

    // Weather-compensated feed-forward base from the outside temperature.
    float tout = temps_ ? temps_->outside() : state_.get_outside_temp();
    bool  tout_valid = state_.is_outside_valid();
    float wc_cold = state_.get_wc_sp_cold();
    float wc_warm = state_.get_wc_sp_warm();
    float base;
    if (!tout_valid || !(tout > -60.0f && tout < 60.0f)) {
        base = WC_FALLBACK;                       // never read, NaN or out of range
    } else if (tout <= WC_OUT_COLD) {
        base = wc_cold;
    } else if (tout >= WC_OUT_WARM) {
        base = wc_warm;
    } else {
        float f = (tout - WC_OUT_COLD) / (WC_OUT_WARM - WC_OUT_COLD);
        base = wc_cold + (wc_warm - wc_cold) * f;
    }
    if (base < sp_lo_) base = sp_lo_;
    if (base > sp_hi_) base = sp_hi_;
    base_ = base;

    // The PI trim is bounded around the weather base and by the SP range.
    float trim_lo = sp_lo_ - base_;
    float trim_hi = sp_hi_ - base_;
    if (trim_lo < -TRIM_MAX) trim_lo = -TRIM_MAX;
    if (trim_hi >  TRIM_MAX) trim_hi =  TRIM_MAX;
    if (trim_hi <= trim_lo) trim_hi = trim_lo + 0.1f;
    pid_cfg_.out_min = trim_lo;
    pid_cfg_.out_max = trim_hi;

    // Freeze integral during DHW. When the room is above target, unwind it
    // faster so the trim does not linger high and cause overshoot.
    float saved_ki = pid_cfg_.ki;
    if (dhw) {
        pid_cfg_.ki = 0.0f;
    } else if (target - room < 0.0f) {
        pid_cfg_.ki = saved_ki * UNWIND_GAIN;
    }

    float trim = pid_step(&pid_cfg_, &pid_state_, target, room, dt);
    pid_cfg_.ki = saved_ki;

    float sp = base_ + trim;
    if (sp < sp_lo_) sp = sp_lo_;
    if (sp > sp_hi_) sp = sp_hi_;

    if (sp <= sp_lo_ || sp >= sp_hi_) {
        if (!clamped_logged_) {
            float error = target - room;
            float p_term = pid_cfg_.kp * error;
            log_.event(ILogger::MODE,
                "PID: SP %.1f упёрлась в [%.0f..%.0f]°C (база=%.1f цель=%.1f комн=%.1f Δ=%.1f P=%.1f I=%.1f dt=%d)",
                (double)sp, (double)sp_lo_, (double)sp_hi_, (double)base_,
                (double)target, (double)room, (double)error,
                (double)p_term, (double)pid_state_.integral, dt);
            clamped_logged_ = true;
        }
    } else {
        clamped_logged_ = false;
    }

    float p_term = pid_cfg_.kp * (target - room);
    float i_term = pid_state_.integral;
    float d_term = pid_state_.d_filt;

    state_.lock_exclusive();
    state_.set_pid_base(base_);
    state_.set_pid_state(state_.get_pid_enabled(), true, sp,
                          p_term, i_term, d_term,
                          room, target,
                          state_.get_pid_cycle_locked(),
                          state_.get_pid_remaining_lockout(),
                          state_.get_pid_ch_enabled_by_pid());
    state_.unlock_exclusive();
}
