#include "application/use_cases/system_config_interactor.h"
#include "application/use_cases/boiler_poll_interactor.h"
#include "application/use_cases/pid_poll_interactor.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/gas_flow_estimator.h"
#include "domain/value_objects/ch_schedule.h"
#include "domain/value_objects/ch_mode.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/itime_settings_store.h"
#include "application/ports/driven/iboiler_config_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include <cstdio>
#include <cstdarg>

SystemConfigInteractor::SystemConfigInteractor(IHeatingStateStore& state, IBoilerHardware& boiler,
                                                 ITimeSettingsStore& config, IBoilerConfigStore& boiler_cfg,
                                                 ILogger& log, ITimeSource& time)
    : state_(state), boiler_(boiler), config_(config), boiler_cfg_(boiler_cfg), log_(log), time_(time)
{
}

void SystemConfigInteractor::save_and_log(const char* msg, ...)
{
    char buf[100];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, sizeof(buf), msg, args);
    va_end(args);
    // Hold shared lock during NVS save to prevent poll task from
    // modifying state mid-read (tearing multi-field config)
    state_.lock_shared();
    log_.event(ILogger::USER, "%s", buf);
    config_.save_time_settings(state_);       // time settings (tz_offset, sntp)
    boiler_cfg_.save_boiler_config(state_);   // boiler config (CH/DHW/PID/calib)
    state_.unlock_shared();
}

// IConfigureSystem
void SystemConfigInteractor::set_ch_mode(CHMode mode)
{
    state_.lock_exclusive();
    state_.set_ch_mode(mode);
    state_.unlock_exclusive();

    const char* mode_name = "?";
    switch (mode) {
        case CHMode::Manual_Static: mode_name = "Ручной статичный"; break;
        case CHMode::PID_Static:    mode_name = "Адаптивный статичный"; break;
        case CHMode::Manual_Sched:  mode_name = "Ручной по расписанию"; break;
        case CHMode::PID_Sched:     mode_name = "Адаптивный по расписанию"; break;
    }
    save_and_log("Режим CH: %s", mode_name);

    if (pid_poll_) {
        if (is_pid_mode(mode))
            pid_poll_->enable();
        else
            pid_poll_->disable();
    }
}

void SystemConfigInteractor::set_ch_enable(bool en)
{
    state_.lock_exclusive();
    state_.set_ch_enable(en);
    state_.unlock_exclusive();
    save_and_log("CH: %s", en ? "вкл" : "выкл");
}

void SystemConfigInteractor::set_dhw_enable(bool en)
{
    state_.lock_exclusive();
    state_.set_dhw_enable(en);
    state_.unlock_exclusive();
    save_and_log("ГВС: %s", en ? "вкл" : "выкл");
}

void SystemConfigInteractor::set_ch_setpoint(float sp)
{
    if (sp < 20.0f) sp = 20.0f;
    if (sp > 80.0f) sp = 80.0f;
    state_.lock_exclusive();
    state_.set_ch_setpoint(sp);
    state_.unlock_exclusive();
    if (boiler_poll_) boiler_poll_->set_ch_setpoint(sp);  // notify poll task immediately
    save_and_log("Уставка CH: %.0f C", (double)sp);
}

void SystemConfigInteractor::set_dhw_setpoint(float sp)
{
    if (sp < 35.0f) sp = 35.0f;
    if (sp > 80.0f) sp = 80.0f;
    state_.lock_exclusive();
    state_.set_dhw_setpoint(sp);
    state_.unlock_exclusive();
    if (boiler_poll_) boiler_poll_->set_dhw_setpoint(sp);  // notify poll task immediately
    save_and_log("Уставка ГВС: %.0f C", (double)sp);
}

void SystemConfigInteractor::set_dhw_hysteresis(float v)
{
    if (v < 0.5f) v = 0.5f;
    if (v > 10.0f) v = 10.0f;
    state_.lock_exclusive();
    state_.set_dhw_hysteresis(v);
    state_.unlock_exclusive();
    save_and_log("Гистерезис ГВС: %.1f C", (double)v);
}

void SystemConfigInteractor::set_schedule(const CH_Schedule& sched)
{
    state_.lock_exclusive();
    state_.set_schedule(&sched);
    state_.unlock_exclusive();
    save_and_log("Расписание: %s", sched.enabled ? "вкл" : "выкл");
}

void SystemConfigInteractor::set_pid_schedule(const PID_Schedule& sched)
{
    state_.lock_exclusive();
    state_.set_pid_schedule(&sched);
    state_.unlock_exclusive();
    save_and_log("PID расписание: %s", sched.enabled ? "вкл" : "выкл");
}

void SystemConfigInteractor::set_timezone(int offset)
{
    state_.lock_exclusive();
    state_.set_tz_offset(offset);
    state_.unlock_exclusive();
    time_.set_timezone(offset);
    save_and_log("Часовой пояс: UTC%+d", offset);
}

void SystemConfigInteractor::set_sntp_servers(const char* srv0, const char* srv1)
{
    state_.lock_exclusive();
    state_.set_sntp_server0(srv0);
    state_.set_sntp_server1(srv1);
    state_.unlock_exclusive();
    save_and_log("NTP серверы: %s, %s", srv0, srv1);
}

// IConfigurePid
void SystemConfigInteractor::set_pid_enable(bool en)
{
    state_.lock_exclusive();
    state_.set_pid_state(en, state_.get_pid_active(), state_.get_pid_output(),
                          state_.get_pid_p(), state_.get_pid_i(), state_.get_pid_d(),
                          state_.get_pid_room_temp(), state_.get_pid_target_room(),
                          state_.get_pid_cycle_locked(), state_.get_pid_remaining_lockout(),
                          state_.get_pid_ch_enabled_by_pid());
    state_.unlock_exclusive();
    save_and_log("PID: %s", en ? "вкл" : "выкл");
}

void SystemConfigInteractor::set_pid_parameters(float kp, float ki, float kd, int dt_sec,
                                                  int room_sensor, float target_room, int lockout_sec)
{
    state_.lock_exclusive();
    state_.set_pid_config(kp, ki, kd, dt_sec, room_sensor, target_room, lockout_sec);
    state_.unlock_exclusive();
    save_and_log("PID params: Kp=%.3f Ki=%.5f Kd=%.3f dt=%d sensor=T%d target=%.1f lockout=%d",
                 (double)kp, (double)ki, (double)kd, dt_sec, room_sensor + 1, (double)target_room, lockout_sec);
}

void SystemConfigInteractor::set_pid_hysteresis(float v)
{
    state_.lock_exclusive();
    state_.set_pid_hysteresis(v);
    state_.unlock_exclusive();
    save_and_log("Гистерезис PID: %.1f C", (double)v);
}

// IFaultReset — fault reset is handled by BoilerPollInteractor::do_status()
// (pending_fault_reset_ flag sent as LB bit 0 in STATUS frame)
void SystemConfigInteractor::reset()
{
    // Set fault reset flag in boiler poll interactor if wired
    if (boiler_poll_) boiler_poll_->trigger_fault_reset();
    log_.event(ILogger::USER, "Сброс ошибки запрошен");
}

// IResetStatistics
void SystemConfigInteractor::reset_modulation_stats()
{
    if (mod_stats_) mod_stats_->reset();
    log_.event(ILogger::USER, "Статистика модуляции сброшена");
}

void SystemConfigInteractor::reset_cycle_stats()
{
    if (burn_cycles_) burn_cycles_->reset();
    log_.event(ILogger::USER, "Статистика циклов сброшена");
}

void SystemConfigInteractor::reset_gas_stats()
{
    if (gas_flow_) gas_flow_->reset();
    log_.event(ILogger::USER, "Статистика газа сброшена");
}
