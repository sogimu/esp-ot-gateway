#include "infrastructure/driven/web_presenter_adapter.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/gas_flow_estimator.h"
#include "infrastructure/driven/event_log_adapter.h"
#include "domain/value_objects/ch_schedule.h"
#include <cstdio>
#include <ctime>
#include <inttypes.h>
#include "esp_timer.h"

int WebPresenterAdapter::render_status(char* buf, size_t size)
{
    if (!state_) return snprintf(buf, size, "{}");

    state_->lock_shared();

    struct tm ti;
    time_t now_ts;
    time(&now_ts);
    localtime_r(&now_ts, &ti);
    char timebuf[16] = "NTP...";
    if (ti.tm_year >= (2024 - 1900))
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);

    // UTC decomposition (independent of TZ) for diagnostics
    struct tm utc_ti;
    gmtime_r(&now_ts, &utc_ti);
    char utcbuf[16] = "NTP...";
    if (utc_ti.tm_year >= (2024 - 1900))
        snprintf(utcbuf, sizeof(utcbuf), "%02d:%02d:%02d", utc_ti.tm_hour, utc_ti.tm_min, utc_ti.tm_sec);

    int len = snprintf(buf, size,
        "{"
        "\"connected\":%d,\"fault\":%d,\"ch_active\":%d,\"dhw_active\":%d,\"flame\":%d,"
        "\"ch_temp\":%.1f,\"return_temp\":%.1f,\"dhw_temp\":%.1f,\"outside_temp\":%.1f,"
        "\"modulation\":%.1f,"
        "\"ch_setpoint\":%.0f,\"dhw_setpoint\":%.0f,"
        "\"dhw_sp_min\":%.0f,\"dhw_sp_max\":%.0f,\"ch_sp_min\":%.0f,\"ch_sp_max\":%.0f,"
        "\"ch_enable\":%d,\"ch_mode\":%d,\"dhw_enable\":%d,"
        "\"dhw_pred_active\":%d,\"dhw_pred_remaining\":%d,\"dhw_pred_uncertainty\":%d,"
        "\"dhw_pred_elapsed\":%d,\"dhw_pred_rate\":%.4f,\"dhw_last_session_sec\":%d,"
        "\"asf_flags\":%d,\"oem_fault\":%d,\"oem_diag\":%d,"
        "\"slave_type\":%d,\"slave_ver\":%d,\"ot_ver\":%.1f,"
        "\"t1_temp\":%.1f,\"t2_temp\":%.1f,"
        "\"sched_on\":0,\"hour\":%d,\"time\":\"%s\",\"tz_offset\":%d,"
        "\"utc_time\":\"%s\",\"uptime_sec\":%lu,"
        "\"dhw_hyst_on\":%.1f,"
        "\"sntp_server0\":\"%s\",\"sntp_server1\":\"%s\","
        "\"pid_enabled\":%d,\"pid_active\":%d,\"pid_output\":%.0f,"
        "\"pid_p\":%.1f,\"pid_i\":%.1f,\"pid_d\":%.1f,"
        "\"pid_room_temp\":%.1f,\"pid_target_room\":%.1f,"
        "\"pid_cycle_locked\":%d,\"pid_remaining_lockout\":%d,\"pid_ch_enabled\":%d,"
        "\"pid_kp\":%.1f,\"pid_ki\":%.4f,\"pid_kd\":%.1f,"
        "\"pid_dt_sec\":%d,\"pid_room_sensor\":%d,\"pid_lockout_sec\":%d,"
        "\"pid_hysteresis\":%.1f"
        "}",
        state_->is_connected() ? 1 : 0,
        state_->has_fault() ? 1 : 0,
        state_->is_ch_active() ? 1 : 0, state_->is_dhw_active() ? 1 : 0,
        state_->is_flame_on() ? 1 : 0,
        (double)state_->get_ch_temp(), (double)state_->get_return_temp(),
        (double)state_->get_dhw_temp(), (double)state_->get_outside_temp(),
        (double)state_->get_modulation(),
        (double)state_->get_ch_setpoint(), (double)state_->get_dhw_setpoint(),
        (double)state_->get_dhw_sp_min(), (double)state_->get_dhw_sp_max(),
        (double)state_->get_ch_sp_min(), (double)state_->get_ch_sp_max(),
        state_->is_ch_enabled() ? 1 : 0, state_->get_ch_mode(),
        state_->is_dhw_enabled() ? 1 : 0,
        state_->get_dhw_pred_active() ? 1 : 0,
        state_->get_dhw_pred_remaining_sec(), state_->get_dhw_pred_uncertainty_sec(),
        state_->get_dhw_pred_elapsed_sec(), (double)state_->get_dhw_pred_rate_cps(),
        state_->get_dhw_last_session_sec(),
        state_->get_asf_flags(), state_->get_oem_fault_code(), state_->get_oem_diagnostic(),
        state_->get_slave_type(), state_->get_slave_version(), (double)state_->get_ot_version(),
        (double)state_->get_t1_temp(), (double)state_->get_t2_temp(),
        (ti.tm_year >= (2024 - 1900)) ? ti.tm_hour : -1, timebuf,
        state_->get_tz_offset(), utcbuf,
        (unsigned long)(esp_timer_get_time() / 1000000),
        (double)state_->get_dhw_hysteresis(),
        state_->get_sntp_server0(), state_->get_sntp_server1(),
        state_->get_pid_enabled() ? 1 : 0, state_->get_pid_active() ? 1 : 0,
        (double)state_->get_pid_output(),
        (double)state_->get_pid_p(), (double)state_->get_pid_i(), (double)state_->get_pid_d(),
        (double)state_->get_pid_room_temp(), (double)state_->get_pid_target_room(),
        state_->get_pid_cycle_locked() ? 1 : 0, state_->get_pid_remaining_lockout(),
        state_->get_pid_ch_enabled_by_pid() ? 1 : 0,
        (double)state_->get_pid_kp(), (double)state_->get_pid_ki(), (double)state_->get_pid_kd(),
        state_->get_pid_dt_sec(), state_->get_pid_room_sensor(), state_->get_pid_lockout_sec(),
        (double)state_->get_pid_hysteresis()
    );

    state_->unlock_shared();
    return len;
}

int WebPresenterAdapter::render_schedule(char* buf, size_t size)
{
    if (!state_) return snprintf(buf, size, "{}");

    state_->lock_shared();
    // Read schedule data from state via opaque get_schedule
    CH_Schedule sched;
    state_->get_schedule(&sched);

    int pos = snprintf(buf, size, "{\"enabled\":%d,\"temps\":[", sched.enabled ? 1 : 0);
    for (int h = 0; h < 24 && pos < (int)size - 10; h++) {
        pos += snprintf(buf + pos, size - pos, "%.1f%s",
                        (double)sched.temps[h], (h < 23) ? "," : "");
    }
    pos += snprintf(buf + pos, size - pos, "]}");
    state_->unlock_shared();
    return pos;
}

int WebPresenterAdapter::render_log(char* buf, size_t size)
{
    if (logger_) {
        auto* elog = static_cast<EventLogAdapter*>(logger_);
        return snprintf(buf, size, "%s", elog->to_json());
    }
    return snprintf(buf, size, "{\"count\":0,\"events\":[]}");
}

int WebPresenterAdapter::render_stats(char* buf, size_t size)
{
    if (!mod_stats_ || !burn_cycles_ || !gas_flow_ || !state_)
        return snprintf(buf, size, "{}");

    float p10p50 = (mod_stats_->p50() > 0) ? mod_stats_->p10() / mod_stats_->p50() : 0;

    return snprintf(buf, size,
        "{"
        "\"samples\":%u,"
        "\"p1\":%.1f,\"p10\":%.1f,\"p25\":%.1f,\"p50\":%.1f,"
        "\"p75\":%.1f,\"p90\":%.1f,\"p99\":%.1f,"
        "\"cycles\":%u,"
        "\"med_burn\":%.0f,\"med_pause\":%.0f,"
        "\"avg_burn\":%.0f,\"avg_pause\":%.0f,"
        "\"burner_h\":%.1f,"
        "\"p90_max\":%.1f,\"p10_p50\":%.1f,\"p99_p90\":%.1f,"
        "\"instant_flow\":%.4f,\"integral_m3\":%.3f,"
        "\"avg_1h\":%.4f,\"avg_3h\":%.4f,\"avg_12h\":%.4f,"
        "\"avg_24h\":%.4f,\"avg_7d\":%.4f,"
        "\"mod_filt\":%.1f,\"t_ret_filt\":%.1f,"
        "\"k_calib\":%.3f,\"p_max\":%.1f,\"gas_cal\":%.1f,"
        "\"gas_meter_base\":%.3f,\"gas_meter_total\":%.3f,"
        "\"corrections\":[]"
        "}",
        (unsigned)mod_stats_->samples(),
        (double)mod_stats_->p1(), (double)mod_stats_->p10(),
        (double)mod_stats_->p25(), (double)mod_stats_->p50(),
        (double)mod_stats_->p75(), (double)mod_stats_->p90(), (double)mod_stats_->p99(),
        (unsigned)burn_cycles_->cycle_count(),
        (double)burn_cycles_->median_burn(), (double)burn_cycles_->median_pause(),
        (double)burn_cycles_->avg_burn(), (double)burn_cycles_->avg_pause(),
        (double)burn_cycles_->burner_hours(),
        (double)mod_stats_->p90(), (double)p10p50,
        (double)(mod_stats_->p99() - mod_stats_->p90()),
        (double)gas_flow_->instant_flow(), (double)gas_flow_->integral_m3(),
        (double)gas_flow_->avg_1h(), (double)gas_flow_->avg_3h(),
        (double)gas_flow_->avg_12h(), (double)gas_flow_->avg_24h(),
        (double)gas_flow_->avg_7d(),
        (double)gas_flow_->mod_filtered(), (double)gas_flow_->t_ret_filtered(),
        (double)gas_flow_->k_calib(),
        (double)state_->get_p_max(), (double)state_->get_gas_calorific(),
        (double)state_->get_gas_meter_base(),
        (double)(state_->get_gas_meter_base() + gas_flow_->integral_m3())
    );
}
