#include "infrastructure/driven/web_presenter_adapter.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "domain/value_objects/gas_correction_metrics.h"
#include "application/services/pid_quality_assessor.h"
#include "application/services/fopdt_estimator.h"
#include "application/ports/driven/ievent_log_reader.h"
#include "domain/value_objects/ch_schedule.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <inttypes.h>

// Chrono-based calendar decomposition (no <ctime> needed)
namespace {
struct ChronoDate { int year, mon, day, hour, min, sec; };

ChronoDate civil_from_seconds(int64_t secs) {
    // Convert Unix seconds to civil date using Howard Hinnant's algorithm
    // (public domain, basis for C++20 std::chrono calendar)
    int64_t z = secs / 86400 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    int64_t mp = (5*doy + 2) / 153;
    int d = doy - (153*mp + 2)/5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    int64_t sod = secs % 86400;
    if (sod < 0) sod += 86400;
    return {static_cast<int>(y), m, d,
            static_cast<int>(sod / 3600),
            static_cast<int>((sod % 3600) / 60),
            static_cast<int>(sod % 60)};
}
} // namespace

int WebPresenterAdapter::render_status(char* buf, size_t size)
{
    if (!state_) return snprintf(buf, size, "{}");

    state_->lock_shared();

    char timebuf[16] = "NTP...";
    char utcbuf[16] = "NTP...";
    int sched_hour = -1;
    if (time_) {
        auto local = time_->local_now().time_since_epoch();
        auto lh = std::chrono::duration_cast<std::chrono::hours>(local) % 24;
        sched_hour = (int)lh.count();
        auto lm = std::chrono::duration_cast<std::chrono::minutes>(local % std::chrono::hours(1));
        auto ls = std::chrono::duration_cast<std::chrono::seconds>(local % std::chrono::minutes(1));
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                 (int)lh.count(), (int)lm.count(), (int)ls.count());
        // UTC time
        auto utc = time_->now().time_since_epoch();
        auto uh = std::chrono::duration_cast<std::chrono::hours>(utc) % 24;
        auto um = std::chrono::duration_cast<std::chrono::minutes>(utc % std::chrono::hours(1));
        auto us = std::chrono::duration_cast<std::chrono::seconds>(utc % std::chrono::minutes(1));
        snprintf(utcbuf, sizeof(utcbuf), "%02d:%02d:%02d",
                 (int)uh.count(), (int)um.count(), (int)us.count());
    }

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
        "\"utc_time\":\"%s\",\"uptime_sec\":%lu,\"total_uptime_sec\":%lu,"
        "\"dhw_hyst_on\":%.1f,"
        "\"time_synced\":%d,"
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
        state_->is_ch_enabled() ? 1 : 0, static_cast<int>(state_->get_ch_mode()),
        state_->is_dhw_enabled() ? 1 : 0,
        state_->get_dhw_pred_active() ? 1 : 0,
        state_->get_dhw_pred_remaining_sec(), state_->get_dhw_pred_uncertainty_sec(),
        state_->get_dhw_pred_elapsed_sec(), (double)state_->get_dhw_pred_rate_cps(),
        state_->get_dhw_last_session_sec(),
        state_->get_asf_flags(), state_->get_oem_fault_code(), state_->get_oem_diagnostic(),
        state_->get_slave_type(), state_->get_slave_version(), (double)state_->get_ot_version(),
        (double)state_->get_t1_temp(), (double)state_->get_t2_temp(),
        sched_hour, timebuf,
        state_->get_tz_offset(), utcbuf,
        (unsigned long)(time_ ? time_->monotonic_us() / 1000000 : 0),
        (unsigned long)(total_uptime_base_ + (time_ ? time_->monotonic_us() / 1000000 : 0)),
        (double)state_->get_dhw_hysteresis(),
        time_ ? (time_->is_synced() ? 1 : 0) : 0,
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

int WebPresenterAdapter::render_pid_schedule(char* buf, size_t size)
{
    if (!state_) return snprintf(buf, size, "{}");

    state_->lock_shared();
    PID_Schedule sched;
    state_->get_pid_schedule(&sched);

    // Current local hour from ITimeSource
    int hour = -1;
    if (time_) {
        auto local = time_->local_now().time_since_epoch();
        hour = std::chrono::duration_cast<std::chrono::hours>(local).count() % 24;
    }

    int pos = snprintf(buf, size, "{\"enabled\":%d,\"hour\":%d,\"temps\":[",
                       sched.enabled ? 1 : 0, hour);
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
    if (log_reader_) {
        return snprintf(buf, size, "%s", log_reader_->to_json());
    }
    return snprintf(buf, size, "{\"count\":0,\"events\":[]}");
}

void WebPresenterAdapter::log_lock()
{
    if (log_reader_) log_reader_->lock();
}
void WebPresenterAdapter::log_unlock()
{
    if (log_reader_) log_reader_->unlock();
}

const char* WebPresenterAdapter::log_json()
{
    if (log_reader_) return log_reader_->to_json();
    return nullptr;
}

float WebPresenterAdapter::compute_monthly_error_pct() const
{
    if (!gas_corr_) return 0.0f;
    const NvsMeterBlob& blob = gas_corr_->meter_blob();
    int cnt = blob.corrections_count;
    if (cnt < 2) return 0.0f;
    int head = blob.corrections_head;
    int idx_last = (head - 1 + CORRECTION_LOG_SIZE) % CORRECTION_LOG_SIZE;
    const auto& last = blob.corrections[idx_last];
    int idx_prev = (head - 2 + CORRECTION_LOG_SIZE) % CORRECTION_LOG_SIZE;
    const auto& prev = blob.corrections[idx_prev];
    // Delegate to domain function
    auto m = compute_correction_metrics(
        prev.actual_reading, prev.estimated_total,
        last.actual_reading, last.estimated_total);
    return m.error_pct;
}

int WebPresenterAdapter::render_stats(char* buf, size_t size)
{
    if (!mod_stats_ || !burn_cycles_ || !gas_flow_ || !state_)
        return snprintf(buf, size, "{}");

    float p10p50 = (mod_stats_->p50() > 0) ? mod_stats_->p10() / mod_stats_->p50() : 0;

    int pos = snprintf(buf, size,
        "{"
        "\"samples\":%u,"
        "\"p1\":%.1f,\"p10\":%.1f,\"p25\":%.1f,\"p50\":%.1f,"
        "\"p75\":%.1f,\"p90\":%.1f,\"p99\":%.1f,"
        "\"cycles\":%u,"
        "\"avg_burn\":%.0f,"
        "\"avg_inter_session_pause\":%.0f,\"avg_modulation_pause\":%.0f,"
        "\"inter_session_cnt\":%u,\"modulation_cnt\":%u,"
        "\"burner_h\":%.1f,\"total_pause_h\":%.1f,"
        "\"p90_max\":%.1f,\"p10_p50\":%.1f,\"p99_p90\":%.1f,"
        "\"instant_flow\":%.4f,\"integral_m3\":%.3f,"
        "\"avg_1h\":%.4f,\"avg_3h\":%.4f,\"avg_12h\":%.4f,"
        "\"avg_24h\":%.4f,\"avg_7d\":%.4f,"
        "\"mod_filt\":%.1f,\"t_ret_filt\":%.1f,"
        "\"k_calib\":%.3f,\"p_max\":%.1f,\"gas_cal\":%.1f,"
        "\"gas_temp_offset\":%.1f,"
        "\"ch_pmin\":%.1f,\"ch_pmax\":%.1f,"
        "\"dhw_pmin\":%.1f,\"dhw_pmax\":%.1f,"
        "\"eff_t1\":%.0f,\"eff_v1\":%.2f,"
        "\"eff_t2\":%.0f,\"eff_v2\":%.2f,"
        "\"eff_t3\":%.0f,\"eff_v3\":%.2f,"
        "\"gas_meter_base\":%.3f,\"gas_meter_total\":%.3f,"
        "\"gas_error_pct\":%.1f,"
        "\"total_uptime_sec\":%lu,"
        "\"corrections\":[",
        (unsigned)mod_stats_->samples(),
        (double)mod_stats_->p1(), (double)mod_stats_->p10(),
        (double)mod_stats_->p25(), (double)mod_stats_->p50(),
        (double)mod_stats_->p75(), (double)mod_stats_->p90(), (double)mod_stats_->p99(),
        (unsigned)burn_cycles_->cycle_count(),
        (double)burn_cycles_->avg_burn_sec(),
        (double)burn_cycles_->avg_inter_session_pause_sec(),
        (double)burn_cycles_->avg_modulation_pause_sec(),
        (unsigned)burn_cycles_->inter_session_cnt(),
        (unsigned)burn_cycles_->modulation_cnt(),
        (double)burn_cycles_->burner_hours(),
        (double)(burn_cycles_->total_pause_seconds() / 3600.0f),
        (double)mod_stats_->p90(), (double)p10p50,
        (double)(mod_stats_->p99() - mod_stats_->p90()),
        (double)gas_flow_->instant_flow(), (double)gas_flow_->integral_m3(),
        (double)gas_flow_->avg_1h(), (double)gas_flow_->avg_3h(),
        (double)gas_flow_->avg_12h(), (double)gas_flow_->avg_24h(),
        (double)gas_flow_->avg_7d(),
        (double)gas_flow_->mod_filtered(), (double)gas_flow_->t_ret_filtered(),
        (double)gas_flow_->k_calib(),
        (double)state_->get_p_max(), (double)state_->get_gas_calorific(),
        (double)state_->get_gas_temp_offset(),
        (double)state_->get_ch_pmin(), (double)state_->get_ch_pmax(),
        (double)state_->get_dhw_pmin(), (double)state_->get_dhw_pmax(),
        (double)state_->get_eff_t1(), (double)state_->get_eff_v1(),
        (double)state_->get_eff_t2(), (double)state_->get_eff_v2(),
        (double)state_->get_eff_t3(), (double)state_->get_eff_v3(),
        (double)state_->get_gas_meter_base(),
        (double)(state_->get_gas_meter_base() + gas_flow_->integral_m3()),
        (double)compute_monthly_error_pct(),
        (unsigned long)(total_uptime_base_ + (time_ ? time_->monotonic_us() / 1000000 : 0))
    );

    // Render last 10 corrections, oldest first
    if (gas_corr_ && pos < (int)size - 4) {
        const NvsMeterBlob& blob = gas_corr_->meter_blob();
        int cnt = blob.corrections_count;
        int head = blob.corrections_head;
        int show = cnt < 10 ? cnt : 10;
        int start = cnt - show;  // skip older entries beyond last 10
        for (int i = start; i < cnt && pos < (int)size - 100; i++) {
            // Ring buffer: oldest first within the last 10
            int idx = (head - cnt + i + CORRECTION_LOG_SIZE) % CORRECTION_LOG_SIZE;
            const NvsCorrLogEntry& e = blob.corrections[idx];
            // Format timestamp as HH:MM:SS + date
            char tbuf[16] = "--:--:--";
            char dbuf[16] = "";
            if (e.timestamp > 0) {
                int64_t secs = e.timestamp + time_->tz_offset() * 3600LL;
                auto cd = civil_from_seconds(secs);
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", cd.hour, cd.min, cd.sec);
                snprintf(dbuf, sizeof(dbuf), "%02d.%02d", cd.day, cd.mon);
            }
            pos += snprintf(buf + pos, size - pos,
                "%s{\"ts\":%u,\"t\":\"%s\",\"d\":\"%s\",\"ar\":%.3f,\"et\":%.3f,"
                "\"diff\":%.3f,\"pk\":%.4f,\"nk\":%.4f}",
                (i > start) ? "," : "",
                (unsigned)e.timestamp,
                tbuf, dbuf,
                (double)e.actual_reading, (double)e.estimated_total,
                (double)e.difference,
                (double)e.prev_k_calib, (double)e.new_k_calib);
        }
    }
    pos += snprintf(buf + pos, size - pos, "]}");

    return pos;
}

int WebPresenterAdapter::render_pid_quality(char* buf, size_t size)
{
    if (!pid_quality_) return snprintf(buf, size, "{}");

    const QualityScores& s = pid_quality_->scores();

    int pos = snprintf(buf, size,
        "{"
        "\"composite\":%.1f,"
        "\"overshoot\":%.1f,\"steady_state\":%.1f,\"stability\":%.1f,"
        "\"cycling\":%.1f,\"clamp\":%.1f,"
        "\"fopdt\":{",
        (double)s.composite,
        (double)s.overshoot, (double)s.steady_state, (double)s.stability,
        (double)s.cycling, (double)s.clamp);

    if (fopdt_) {
        pos += snprintf(buf + pos, size - pos,
            "\"gain\":%.2f,\"tau_heat_sec\":%.0f,\"tau_cool_sec\":%.0f,"
            "\"dead_time_sec\":%.0f,\"outside_temp\":%.1f,\"event_count\":%d",
            (double)fopdt_->gain(), (double)fopdt_->time_constant_heat_sec(),
            (double)fopdt_->time_constant_cool_sec(), (double)fopdt_->dead_time_sec(),
            (double)fopdt_->outside_temp_typical(), fopdt_->event_count());
    }

    pos += snprintf(buf + pos, size - pos, "}}");
    return pos;
}
