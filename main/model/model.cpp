#include "model.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_timer.h"

Model::Model()
    : connected_(false)
    , fault_(false), flame_(false), ch_active_(false), dhw_active_(false)
    , ch_temp_(0), dhw_temp_(0), return_temp_(0), outside_temp_(0)
    , modulation_(0)
    , t1_temp_(-127.0f), t2_temp_(-127.0f)
    , ch_setpoint_(30.0f), dhw_setpoint_(55.0f)
    , ch_sp_min_(0), ch_sp_max_(80)
    , dhw_sp_min_(0), dhw_sp_max_(65)
    , ch_enable_(false), dhw_enable_(false)
    , asf_flags_(0), oem_fault_code_(0), oem_diagnostic_(0)
    , burner_starts_(0), ch_pump_starts_(0), dhw_valve_starts_(0), dhw_burner_starts_(0)
    , burner_hours_(0), ch_pump_hours_(0), dhw_valve_hours_(0), dhw_burner_hours_(0)
    , slave_type_(0), slave_version_(0), ot_version_(0)
    , dhw_last_session_sec_(0)
    , dhw_priority_(false), dhw_session_start_ms_(0), dhw_session_min_temp_(0)
    , tz_offset_(3)
{
    schedule_.enabled = false;
    float default_temps[24] = {30,30,30,30,30,30, 35,40,40,35,35,35,
                               35,35,35,35,35,40, 40,40,40,35,35,30};
    for (int i = 0; i < 24; i++) schedule_.temps[i] = default_temps[i];
}

void Model::set_connected(bool v) { connected_ = v; }
void Model::set_fault(bool v) { fault_ = v; }
void Model::set_flame(bool v) { flame_ = v; }
void Model::set_ch_active(bool v) { ch_active_ = v; }
void Model::set_dhw_active(bool v) { dhw_active_ = v; }
void Model::set_ch_temp(float v) { ch_temp_ = v; }
void Model::set_dhw_temp(float v) { dhw_temp_ = v; }
void Model::set_return_temp(float v) { return_temp_ = v; }
void Model::set_outside_temp(float v) { outside_temp_ = v; }
void Model::set_modulation(float v) { modulation_ = v; }
void Model::set_t1_temp(float v) { t1_temp_ = v; }
void Model::set_t2_temp(float v) { t2_temp_ = v; }
void Model::set_ch_setpoint(float v) { ch_setpoint_ = v; }
void Model::set_dhw_setpoint(float v) { dhw_setpoint_ = v; }
void Model::set_ch_sp_min(float v) { ch_sp_min_ = v; }
void Model::set_ch_sp_max(float v) { ch_sp_max_ = v; }
void Model::set_dhw_sp_min(float v) { dhw_sp_min_ = v; }
void Model::set_dhw_sp_max(float v) { dhw_sp_max_ = v; }
void Model::set_ch_enable(bool v) { ch_enable_ = v; }
void Model::set_dhw_enable(bool v) { dhw_enable_ = v; }
void Model::set_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) {
    asf_flags_ = asf; oem_fault_code_ = oem_fault; oem_diagnostic_ = oem_diag;
}
void Model::set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) {
    burner_starts_ = bs; ch_pump_starts_ = cps; dhw_valve_starts_ = dvs; dhw_burner_starts_ = dbs;
}
void Model::set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) {
    burner_hours_ = bh; ch_pump_hours_ = cph; dhw_valve_hours_ = dvh; dhw_burner_hours_ = dbh;
}
void Model::set_version(uint8_t st, uint8_t sv, float ov) {
    slave_type_ = st; slave_version_ = sv; ot_version_ = ov;
}
void Model::set_dhw_session_finished(uint32_t dur_ms, float min_temp) {
    (void)min_temp; dhw_last_session_sec_ = (int)(dur_ms / 1000);
    dhw_priority_ = false;
}
void Model::set_dhw_priority(bool active, uint32_t session_start_ms, float min_temp) {
    dhw_priority_ = active;
    dhw_session_start_ms_ = session_start_ms;
    dhw_session_min_temp_ = min_temp;
}
void Model::set_schedule(const CH_Schedule& sched) { schedule_ = sched; }
void Model::set_tz_offset(int v) { tz_offset_ = v; }

void Model::set_stats(const StatsData& s) { stats_ = s; }
const StatsData& Model::get_stats() const { return stats_; }

std::string Model::to_stats_json() const
{
    char buf[1024];
    float p10p50 = (stats_.p50 > 0) ? stats_.p10 / stats_.p50 : 0;
    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"samples\":%d,"
        "\"p1\":%.1f,\"p10\":%.1f,\"p25\":%.1f,\"p50\":%.1f,"
        "\"p75\":%.1f,\"p90\":%.1f,\"p99\":%.1f,"
        "\"cycles\":%d,"
        "\"med_burn\":%.0f,\"med_pause\":%.0f,"
        "\"avg_burn\":%.0f,\"avg_pause\":%.0f,"
        "\"burner_h\":%.1f,"
        "\"p90_max\":%.1f,\"p10_p50\":%.1f,\"p99_p90\":%.1f"
        "}",
        stats_.sample_count,
        (double)stats_.p1, (double)stats_.p10,
        (double)stats_.p25, (double)stats_.p50,
        (double)stats_.p75, (double)stats_.p90, (double)stats_.p99,
        stats_.cycle_count,
        (double)stats_.median_burn, (double)stats_.median_pause,
        (double)stats_.avg_burn, (double)stats_.avg_pause,
        (double)stats_.burner_hours,
        (double)stats_.p90, (double)p10p50,
        (double)(stats_.p99 - stats_.p90)
    );
    return std::string(buf, (size_t)len);
}

void Model::add_log_entry(uint32_t time_sec, uint8_t cat, const char* msg)
{
    int idx = log_head_;
    log_head_ = (log_head_ + 1) % LOG_RING_SIZE;
    if (log_count_ < LOG_RING_SIZE) log_count_++;
    LogEntry& e = log_ring_[idx];
    e.time_sec = time_sec;
    e.category = cat;
    size_t n = 0;
    for (const char* s = msg; *s && n < sizeof(e.msg) - 1; s++, n++)
        e.msg[n] = *s;
    e.msg[n] = '\0';
}

std::string Model::to_log_json() const
{
    static char buf[24576];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"count\":%d,\"events\":[", log_count_);

    int start = (log_count_ < LOG_RING_SIZE) ? 0 : log_head_;
    int total = log_count_;

    for (int i = 0; i < total && pos < (int)sizeof(buf) - 128; i++) {
        int idx = (start + i) % LOG_RING_SIZE;
        const LogEntry& e = log_ring_[idx];

        struct tm ti;
        char tbuf[16] = "??:??:??";
        if (e.time_sec > 0) {
            time_t t = (time_t)e.time_sec;
            localtime_r(&t, &ti);
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
        }

        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"t\":\"%s\",\"c\":%d,\"m\":\"",
                        i ? "," : "", tbuf, e.category);

        for (const char* s = e.msg; *s && pos < (int)sizeof(buf) - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            buf[pos++] = *s;
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"}");
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    return std::string(buf, (size_t)pos);
}

std::string Model::to_json() const
{
    char buf[2048];
    char timebuf[16] = "NTP...";

    time_t now_ts;
    struct tm ti;
    time(&now_ts);
    localtime_r(&now_ts, &ti);
    if (ti.tm_year >= (2024 - 1900)) {
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    }

    int dhw_session_sec = -1;
    int dhw_est_total_sec = -1;
    if (dhw_priority_ && dhw_session_start_ms_ > 0) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms > dhw_session_start_ms_) {
            dhw_session_sec = (int)((now_ms - dhw_session_start_ms_) / 1000);
            if (dhw_session_sec >= 10 && dhw_temp_ > dhw_session_min_temp_) {
                float rate = (dhw_temp_ - dhw_session_min_temp_) / (float)dhw_session_sec;
                if (rate > 0) {
                    dhw_est_total_sec = (int)((dhw_setpoint_ - dhw_session_min_temp_) / rate);
                }
            }
        }
    }

    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"connected\":%d,"
        "\"fault\":%d,"
        "\"ch_active\":%d,"
        "\"dhw_active\":%d,"
        "\"flame\":%d,"
        "\"ch_temp\":%.1f,"
        "\"return_temp\":%.1f,"
        "\"dhw_temp\":%.1f,"
        "\"outside_temp\":%.1f,"
        "\"modulation\":%.1f,"
        "\"ch_setpoint\":%.0f,"
        "\"dhw_setpoint\":%.0f,"
        "\"dhw_sp_min\":%.0f,"
        "\"dhw_sp_max\":%.0f,"
        "\"ch_sp_min\":%.0f,"
        "\"ch_sp_max\":%.0f,"
        "\"ch_enable\":%d,"
        "\"dhw_enable\":%d,"
        "\"dhw_priority\":%d,"
        "\"dhw_session_sec\":%d,"
        "\"dhw_est_total_sec\":%d,"
        "\"dhw_last_session_sec\":%d,"
        "\"asf_flags\":%d,"
        "\"oem_fault\":%d,"
        "\"oem_diag\":%d,"
        "\"slave_type\":%d,"
        "\"slave_ver\":%d,"
        "\"ot_ver\":%.1f,"
        "\"t1_temp\":%.1f,"
        "\"t2_temp\":%.1f,"
        "\"sched_on\":%d,"
        "\"hour\":%d,"
        "\"time\":\"%s\","
        "\"tz_offset\":%d"
        "}",
        connected_ ? 1 : 0,
        fault_ ? 1 : 0,
        ch_active_ ? 1 : 0,
        dhw_active_ ? 1 : 0,
        flame_ ? 1 : 0,
        (double)ch_temp_,
        (double)return_temp_,
        (double)dhw_temp_,
        (double)outside_temp_,
        (double)modulation_,
        (double)ch_setpoint_,
        (double)dhw_setpoint_,
        (double)dhw_sp_min_,
        (double)dhw_sp_max_,
        (double)ch_sp_min_,
        (double)ch_sp_max_,
        ch_enable_ ? 1 : 0,
        dhw_enable_ ? 1 : 0,
        dhw_priority_ ? 1 : 0,
        dhw_session_sec,
        dhw_est_total_sec,
        dhw_last_session_sec_,
        asf_flags_,
        oem_fault_code_,
        oem_diagnostic_,
        slave_type_,
        slave_version_,
        (double)ot_version_,
        (double)t1_temp_,
        (double)t2_temp_,
        schedule_.enabled ? 1 : 0,
        (ti.tm_year >= (2024 - 1900)) ? ti.tm_hour : -1,
        timebuf,
        tz_offset_
    );
    return std::string(buf, (size_t)len);
}
