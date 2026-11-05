#include "application/services/gas_flow_estimator.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/iheating_stats_store.h"
#include "application/ports/driven/igas_correction_store.h"
#include "application/ports/driven/itemperatures_source.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr int32_t RAW_STORAGE_OFFSET = 100;

GasFlowService::GasFlowService(IHeatingStateStore& state, ITimeSource& time, IHeatingStatsStore& store,
                                 IGasCorrectionStore& gas_store, ITemperatureSource* temps)
    : state_(state), time_(time), store_(store), gas_store_(gas_store), temps_(temps)
{
}

void GasFlowService::load_integral()
{
    uint32_t bs=0; float im3=0;
    if (store_.load_stats(bs, im3, nullptr, nullptr, nullptr, nullptr))
        integral_m3_ = im3;
}

float GasFlowService::efficiency_continuous(float t_ret) const
{
    float t1 = state_.get_eff_t1();
    float v1 = state_.get_eff_v1();
    float t2 = state_.get_eff_t2();
    float v2 = state_.get_eff_v2();
    float t3 = state_.get_eff_t3();
    float v3 = state_.get_eff_v3();

    if (t_ret <= t1) return v1;
    if (t_ret <= t2) {
        return v1 + (v2 - v1) * (t_ret - t1) / (t2 - t1);
    }
    if (t_ret <= t3) {
        return v2 + (v3 - v2) * (t_ret - t2) / (t3 - t2);
    }
    return v3;
}

float GasFlowService::corrected_calorific() const
{
    // If outdoor temperature not available (sensor not present/never read), use configured CV
    if (!outdoor_temp_valid_) {
        return gas_calorific_;
    }
    // gas_temp_offset from state (user-configured, default -5.0C for buried gas pipe)
    float offset = state_.get_gas_temp_offset();
    float t_gas = outdoor_temp_ + offset;
    // Temperature correction factor (Boyle's law, standard reference +15C = 288.15K)
    float f_t = (15.0f + 273.15f) / (t_gas + 273.15f);
    return gas_calorific_ * f_t;
}

float GasFlowService::calc_power(float modulation_pct, float /*flow_temp*/, float /*ret_temp*/) const
{
    // Clamp modulation to valid range
    if (modulation_pct < 0.0f) modulation_pct = 0.0f;
    if (modulation_pct > 100.0f) modulation_pct = 100.0f;

    // The burner-off case (modulation at/below FIRING_EPS) is handled by
    // firing-gating in execute() (→ latest_flow_ = 0), so this mapping is
    // only reached for a firing burner: power runs from pmin at the smallest
    // reported modulation up to pmax.

    // Input (nameplate) power: modulation % is relative to nameplate max.
    // Does not depend on MWT — gas valve delivers the same gas at the same modulation.
    float pmin = dhw_active_ ? state_.get_dhw_pmin() : state_.get_ch_pmin();
    float pmax = dhw_active_ ? state_.get_dhw_pmax() : state_.get_ch_pmax();
    return pmin + (pmax - pmin) * modulation_pct / 100.0f;
}

void GasFlowService::execute()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.monotonic_ms());
    if (last_update_ms_ == 0) {
        last_update_ms_ = now_ms;
        return;
    }

    state_.lock_shared();
    float mod_raw = state_.get_modulation();
    float gas_cal = state_.get_gas_calorific();
    bool dhw      = state_.is_dhw_active();
    state_.unlock_shared();
    float t_ret   = temps_ ? temps_->ch_return() : state_.get_return_temp();
    float t_flow  = temps_ ? temps_->ch_flow()   : state_.get_ch_temp();
    float t_out   = temps_ ? temps_->outside()   : state_.get_outside_temp();

    dhw_active_ = dhw;

    if (gas_cal > 0) gas_calorific_ = gas_cal;
    // Mark temperature channels valid once non-zero data arrives
    // (CH temps are always >0 in a working system; 0 means "never read")
    if (t_flow > 0.0f) flow_temp_valid_ = true;
    if (t_ret  > 0.0f) ret_temp_valid_  = true;
    // Update outdoor temperature if value is plausible
    if (t_out >= -50.0f && t_out <= 60.0f) {
        outdoor_temp_ = t_out;
        outdoor_temp_valid_ = true;
    }

    // ── Sensor disconnect protection: invalidate outdoor_temp after 6h of steady 0 ──
    if (t_out == 0.0f && outdoor_temp_valid_) {
        if (outdoor_zero_start_ms_ == 0) {
            outdoor_zero_start_ms_ = now_ms;
        } else if (now_ms - outdoor_zero_start_ms_ > 6u * 3600000u) {
            outdoor_temp_valid_ = false;
            outdoor_temp_ = 0.0f;
        }
    } else {
        outdoor_zero_start_ms_ = 0;
    }

    // ── Firing-gating: modulation, not the (unreliable) flame bit ──
    bool firing = mod_raw > FIRING_EPS;
    if (!firing_prev_ && firing) {
        ignition_start_ms_ = now_ms;
        kalman_mod_.reset(mod_raw);
    }
    firing_prev_ = firing;

    if (firing) {
        // Kalman filter raw inputs (only while firing — prevents filter drift on zeros)
        float mod_f = kalman_mod_.update(mod_raw);
        float ret_f = kalman_ret_.update(t_ret);
        mod_filtered_val = mod_f;
        t_ret_filtered_val = ret_f;

        // Warmup factor: linear ramp 0.85→1.0 over first 60s after ignition
        float warmup = 1.0f;
        uint32_t elapsed_ms = now_ms - ignition_start_ms_;
        if (elapsed_ms < 60000u) {
            warmup = 0.85f + 0.15f * (static_cast<float>(elapsed_ms) / 60000.0f);
        }

        // Physical model: flow [m3/h] = k * power_kw_input / cv * warmup
        // Input power is nameplate — no efficiency needed (gas meter measures input energy)
        float power_kw = calc_power(mod_f, t_flow, ret_f);
        float cv_eff = corrected_calorific();
        float flow_raw = (power_kw / cv_eff) * warmup;
        if (flow_raw < 0) flow_raw = 0;
        latest_flow_ = k_calib_ * flow_raw;

        // Integrate flow (only while firing)
        uint32_t dt_ms = now_ms - last_update_ms_;
        if (dt_ms > 0 && dt_ms < 60000) {
            float dt_h = static_cast<float>(dt_ms) / 3600000.0f;
            float dv_raw = flow_raw * dt_h;
            integral_m3_ += k_calib_ * dv_raw;
            daily_accumulator_ += dv_raw;   // raw (K=1), scaled at view time
            hourly_accumulator_ += dv_raw;  // current hour bucket
            if (dhw_active_) {              // split: DHW (boiler) vs heating
                daily_accumulator_dhw_ += dv_raw;
                hourly_accumulator_dhw_ += dv_raw;
            }
        }
    } else {
        latest_flow_ = 0;
    }
    last_update_ms_ = now_ms;

    // Time-bucket tracking only once wall clock is synced (NTP/manual).
    // Hourly runs first: at a day boundary it archives the last hour of the
    // outgoing day before update_daily_tracking() moves today's hours to
    // yesterday.
    if (time_.is_synced()) {
        update_hourly_tracking();
        update_daily_tracking();
    }
}

void GasFlowService::update_hourly_tracking()
{
    auto local_sec = std::chrono::duration_cast<std::chrono::seconds>(
        time_.local_now().time_since_epoch()).count();
    int64_t epoch_hour = local_sec / 3600;

    if (today_epoch_hour_ < 0) {
        today_epoch_hour_ = epoch_hour;
        hourly_accumulator_ = 0;
        hourly_accumulator_dhw_ = 0;
        return;
    }
    if (epoch_hour == today_epoch_hour_) return;

    // Archive the hour we are leaving — only if it belongs to the currently
    // tracked day (otherwise it is a clock jump already handled by the day
    // boundary logic below).
    if (today_epoch_hour_ / 24 == today_epoch_day_) {
        int idx = static_cast<int>(today_epoch_hour_ % 24);
        if (idx >= 0 && idx < HOURS_PER_DAY) {
            today_hours_[idx] += hourly_accumulator_;
            today_hours_dhw_[idx] += hourly_accumulator_dhw_;
        }
    }
    today_epoch_hour_ = epoch_hour;
    hourly_accumulator_ = 0;
    hourly_accumulator_dhw_ = 0;
}

void GasFlowService::push_daily(int64_t epoch_day, float m3_total, float m3_dhw)
{
    for (int i = 0; i < daily_count_; i++) {
        GasDailyEntry& e = daily_[(daily_head_ + i) % DAILY_SLOTS];
        if (e.epoch_day == epoch_day) {
            e.m3_total += m3_total;
            e.m3_dhw += m3_dhw;
            return;
        }
    }
    int idx = (daily_head_ + daily_count_) % DAILY_SLOTS;
    daily_[idx].epoch_day = epoch_day;
    daily_[idx].m3_total = m3_total;
    daily_[idx].m3_dhw = m3_dhw;
    if (daily_count_ < DAILY_SLOTS) {
        daily_count_++;
    } else {
        daily_head_ = (daily_head_ + 1) % DAILY_SLOTS;
    }
}

void GasFlowService::normalize_daily_history()
{
    GasDailyEntry tmp[DAILY_SLOTS];
    int n = 0;
    for (int i = 0; i < daily_count_; i++) {
        const GasDailyEntry& e = daily_[(daily_head_ + i) % DAILY_SLOTS];
        int j = 0;
        for (; j < n; j++) {
            if (tmp[j].epoch_day == e.epoch_day) break;
        }
        if (j == n) {
            tmp[n++] = e;
        } else {
            tmp[j].m3_total += e.m3_total;
            tmp[j].m3_dhw += e.m3_dhw;
        }
    }
    for (int i = 1; i < n; i++) {
        GasDailyEntry key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].epoch_day > key.epoch_day) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    int kept = 0;
    for (int i = 0; i < n; i++) {
        if (today_epoch_day_ >= 0 && tmp[i].epoch_day == today_epoch_day_) {
            daily_accumulator_ += tmp[i].m3_total;
            daily_accumulator_dhw_ += tmp[i].m3_dhw;
            continue;
        }
        daily_[kept++] = tmp[i];
    }
    daily_head_ = 0;
    daily_count_ = kept;
}

void GasFlowService::update_daily_tracking()
{
    auto local_sec = std::chrono::duration_cast<std::chrono::seconds>(
        time_.local_now().time_since_epoch()).count();
    int64_t epoch_day = local_sec / 86400;

    if (today_epoch_day_ < 0) {
        today_epoch_day_ = epoch_day;
        daily_accumulator_ = 0;
        daily_accumulator_dhw_ = 0;
        return;
    }

    if (epoch_day != today_epoch_day_) {
        // Day boundary: archive yesterday's consumption
        push_daily(today_epoch_day_, daily_accumulator_, daily_accumulator_dhw_);

        if (epoch_day == today_epoch_day_ + 1) {
            for (int i = 0; i < HOURS_PER_DAY; i++) {
                yesterday_hours_[i] = today_hours_[i];
                yesterday_hours_dhw_[i] = today_hours_dhw_[i];
            }
            yesterday_epoch_day_ = today_epoch_day_;
        } else {
            // Clock jump over one or more days — do not carry stale hours
            for (int i = 0; i < HOURS_PER_DAY; i++) {
                yesterday_hours_[i] = 0;
                yesterday_hours_dhw_[i] = 0;
            }
            yesterday_epoch_day_ = -1;
        }
        for (int i = 0; i < HOURS_PER_DAY; i++) {
            today_hours_[i] = 0;
            today_hours_dhw_[i] = 0;
        }

        today_epoch_day_ = epoch_day;
        daily_accumulator_ = 0;
        daily_accumulator_dhw_ = 0;
        history_dirty_ = true;
    }
}

int GasFlowService::get_daily_view(DailyView* out, int max) const
{
    int n = 0;
    for (int i = 0; i < daily_count_ && n < max; i++) {
        const GasDailyEntry& s = daily_[(daily_head_ + i) % DAILY_SLOTS];
        out[n].epoch_day = s.epoch_day;
        out[n].m3_total = s.m3_total * k_calib_;
        out[n].m3_dhw = s.m3_dhw * k_calib_;
        n++;
    }
    if (n < max && today_epoch_day_ >= 0) {
        out[n].epoch_day = today_epoch_day_;
        out[n].m3_total = daily_accumulator_ * k_calib_;
        out[n].m3_dhw = daily_accumulator_dhw_ * k_calib_;
        n++;
    }
    return n;
}

int GasFlowService::get_hourly_view(HourlyView* out, int max) const
{
    int n = 0;
    if (yesterday_epoch_day_ >= 0) {
        for (int h = 0; h < HOURS_PER_DAY && n < max; h++) {
            out[n].epoch_hour = yesterday_epoch_day_ * 24 + h;
            out[n].m3_total = yesterday_hours_[h] * k_calib_;
            out[n].m3_dhw = yesterday_hours_dhw_[h] * k_calib_;
            n++;
        }
    }
    if (today_epoch_day_ >= 0) {
        int cur_hour = -1;
        if (today_epoch_hour_ >= 0 && today_epoch_hour_ / 24 == today_epoch_day_) {
            cur_hour = static_cast<int>(today_epoch_hour_ % 24);
        }
        for (int h = 0; h < HOURS_PER_DAY && n < max; h++) {
            out[n].epoch_hour = today_epoch_day_ * 24 + h;
            out[n].m3_total = (today_hours_[h] + (h == cur_hour ? hourly_accumulator_ : 0.0f)) * k_calib_;
            out[n].m3_dhw = (today_hours_dhw_[h] + (h == cur_hour ? hourly_accumulator_dhw_ : 0.0f)) * k_calib_;
            n++;
        }
    }
    return n;
}

void GasFlowService::pack_today(GasTodayBlob& blob) const
{
    std::memset(&blob, 0, sizeof(blob));
    blob.today_epoch_day = today_epoch_day_;
    blob.today_m3_total = daily_accumulator_;
    blob.today_m3_dhw = daily_accumulator_dhw_;
    for (int h = 0; h < HOURS_PER_DAY; h++) {
        blob.today_hours_total[h] = today_hours_[h];
        blob.today_hours_dhw[h] = today_hours_dhw_[h];
    }
    int32_t ch = -1;
    if (today_epoch_hour_ >= 0 && today_epoch_hour_ / 24 == today_epoch_day_) {
        ch = static_cast<int32_t>(today_epoch_hour_ % 24);
    }
    blob.current_hour = (ch < 0 ? 99 : ch) + RAW_STORAGE_OFFSET;
    blob.current_hour_m3_total = hourly_accumulator_;
    blob.current_hour_m3_dhw = hourly_accumulator_dhw_;
}

void GasFlowService::pack_history(GasHistoryBlob& blob) const
{
    std::memset(&blob, 0, sizeof(blob));
    for (int i = 0; i < daily_count_; i++) {
        const GasDailyEntry& s = daily_[(daily_head_ + i) % DAILY_SLOTS];
        blob.daily[i].epoch_day = s.epoch_day;
        blob.daily[i].m3_total = s.m3_total;
        blob.daily[i].m3_dhw = s.m3_dhw;
    }
    blob.head = daily_head_ + RAW_STORAGE_OFFSET;
    blob.count = daily_count_;
    blob.yesterday_epoch_day = yesterday_epoch_day_;
    for (int h = 0; h < HOURS_PER_DAY; h++) {
        blob.yesterday_hours_total[h] = yesterday_hours_[h];
        blob.yesterday_hours_dhw[h] = yesterday_hours_dhw_[h];
    }
}

bool GasFlowService::consume_history_dirty()
{
    bool d = history_dirty_;
    history_dirty_ = false;
    return d;
}

void GasFlowService::load_daily()
{
    auto scale_legacy = [this](float v) {
        return (k_calib_ > 0.0f) ? v / k_calib_ : v;
    };

    GasTodayBlob t;
    if (gas_store_.load_today_gas(&t)) {
        bool raw = t.current_hour >= RAW_STORAGE_OFFSET;
        int32_t ch = raw ? t.current_hour - RAW_STORAGE_OFFSET : t.current_hour;
        if (ch == 99) ch = -1;
        today_epoch_day_ = t.today_epoch_day;
        daily_accumulator_ = raw ? t.today_m3_total : scale_legacy(t.today_m3_total);
        daily_accumulator_dhw_ = raw ? t.today_m3_dhw : scale_legacy(t.today_m3_dhw);
        for (int h = 0; h < HOURS_PER_DAY; h++) {
            today_hours_[h] = raw ? t.today_hours_total[h] : scale_legacy(t.today_hours_total[h]);
            today_hours_dhw_[h] = raw ? t.today_hours_dhw[h] : scale_legacy(t.today_hours_dhw[h]);
        }
        hourly_accumulator_ = 0;
        hourly_accumulator_dhw_ = 0;
        today_epoch_hour_ = -1;
        if (t.today_epoch_day >= 0 && ch >= 0 && ch < HOURS_PER_DAY) {
            today_epoch_hour_ = t.today_epoch_day * 24 + ch;
            hourly_accumulator_ = raw ? t.current_hour_m3_total : scale_legacy(t.current_hour_m3_total);
            hourly_accumulator_dhw_ = raw ? t.current_hour_m3_dhw : scale_legacy(t.current_hour_m3_dhw);
        }
    }

    GasHistoryBlob h;
    if (gas_store_.load_history_gas(&h)) {
        bool raw = h.head >= RAW_STORAGE_OFFSET;
        int32_t head = raw ? h.head - RAW_STORAGE_OFFSET : h.head;
        daily_head_ = head % DAILY_SLOTS;
        if (daily_head_ < 0) daily_head_ += DAILY_SLOTS;
        daily_count_ = h.count;
        if (daily_count_ < 0) daily_count_ = 0;
        if (daily_count_ > DAILY_SLOTS) daily_count_ = DAILY_SLOTS;
        for (int i = 0; i < daily_count_; i++) {
            daily_[(daily_head_ + i) % DAILY_SLOTS] = h.daily[i];
            if (!raw) {
                daily_[(daily_head_ + i) % DAILY_SLOTS].m3_total = scale_legacy(h.daily[i].m3_total);
                daily_[(daily_head_ + i) % DAILY_SLOTS].m3_dhw = scale_legacy(h.daily[i].m3_dhw);
            }
        }
        yesterday_epoch_day_ = h.yesterday_epoch_day;
        for (int i = 0; i < HOURS_PER_DAY; i++) {
            yesterday_hours_[i] = raw ? h.yesterday_hours_total[i] : scale_legacy(h.yesterday_hours_total[i]);
            yesterday_hours_dhw_[i] = raw ? h.yesterday_hours_dhw[i] : scale_legacy(h.yesterday_hours_dhw[i]);
        }
    }
    normalize_daily_history();
}

void GasFlowService::reset()
{
    integral_m3_ = 0;
    latest_flow_ = 0;
    kalman_mod_.reset(0);
    kalman_ret_.reset(0);
    firing_prev_ = false;
    ignition_start_ms_ = 0;
    dhw_active_ = false;
    outdoor_temp_valid_ = false;
    outdoor_zero_start_ms_ = 0;
    flow_temp_valid_ = false;
    ret_temp_valid_  = false;
    for (int i = 0; i < DAILY_SLOTS; i++) {
        daily_[i].epoch_day = 0;
        daily_[i].m3_total = 0;
        daily_[i].m3_dhw = 0;
    }
    daily_head_ = 0;
    daily_count_ = 0;
    today_epoch_day_ = -1;
    daily_accumulator_ = 0;
    daily_accumulator_dhw_ = 0;
    history_dirty_ = false;
    today_epoch_hour_ = -1;
    hourly_accumulator_ = 0;
    hourly_accumulator_dhw_ = 0;
    yesterday_epoch_day_ = -1;
    for (int i = 0; i < HOURS_PER_DAY; i++) {
        today_hours_[i] = 0;
        today_hours_dhw_[i] = 0;
        yesterday_hours_[i] = 0;
        yesterday_hours_dhw_[i] = 0;
    }
}
