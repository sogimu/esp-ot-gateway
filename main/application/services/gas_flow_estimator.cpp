#include "application/services/gas_flow_estimator.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driven/iheating_stats_store.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

GasFlowService::GasFlowService(IHeatingStateStore& state, ITimeSource& time, IHeatingStatsStore& store)
    : state_(state), time_(time), store_(store)
{
    ring_ = static_cast<Sample*>(malloc(RING_SIZE * sizeof(Sample)));
    if (ring_) std::memset(ring_, 0, RING_SIZE * sizeof(Sample));
}

GasFlowService::~GasFlowService()
{
    free(ring_);
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

    // Modulation < 1 % means the burner is not actually firing
    // (gas valve closed or in pre-purge).  Real modulating boilers
    // never operate below ~20 % in steady state.
    if (modulation_pct < 1.0f) return 0.0f;

    // Input (nameplate) power: modulation % is relative to nameplate max.
    // Does not depend on MWT — gas valve delivers the same gas at the same modulation.
    float pmin = dhw_active_ ? state_.get_dhw_pmin() : state_.get_ch_pmin();
    float pmax = dhw_active_ ? state_.get_dhw_pmax() : state_.get_ch_pmax();
    return pmin + (pmax - pmin) * modulation_pct / 100.0f;
}

void GasFlowService::update_ema(float& ema, float val, float alpha)
{
    if (ema == 0) ema = val;
    else ema = alpha * val + (1.0f - alpha) * ema;
}

void GasFlowService::execute()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.monotonic_ms());
    if (last_update_ms_ == 0) {
        last_update_ms_ = now_ms;
        ema_start_us_ = time_.monotonic_us();
        return;
    }

    state_.lock_shared();
    float mod_raw = state_.get_modulation();
    float t_ret   = state_.get_return_temp();
    float t_flow  = state_.get_ch_temp();
    float gas_cal = state_.get_gas_calorific();
    float t_out   = state_.get_outside_temp();
    bool flame    = state_.is_flame_on();
    bool dhw      = state_.is_dhw_active();
    state_.unlock_shared();

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

    // ── Flame-gating ──────────────────────────────────────
    if (!flame_prev_ && flame) {
        ignition_start_ms_ = now_ms;
    }
    flame_prev_ = flame;

    if (flame) {
        // Kalman filter raw inputs (only while flame is on — prevents filter drift on zeros)
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
        float flow = k_calib_ * (power_kw / cv_eff) * warmup;
        if (flow < 0) flow = 0;
        latest_flow_ = flow;

        // Integrate flow (only while flame is on)
        uint32_t dt_ms = now_ms - last_update_ms_;
        if (dt_ms > 0 && dt_ms < 60000) {
            float dt_h = static_cast<float>(dt_ms) / 3600000.0f;
            integral_m3_ += flow * dt_h;
            daily_accumulator_ += flow * dt_h;  // daily chart (NOT reset by corrections)
        }
    } else {
        latest_flow_ = 0;
    }
    last_update_ms_ = now_ms;

    // Ring buffer for sliding window EMA
    ring_[ring_idx_].flow = latest_flow_;
    ring_idx_ = (ring_idx_ + 1) % RING_SIZE;
    if (ring_count_ < RING_SIZE) ring_count_++;

    // Compute EMAs every ~10 samples
    ema_tick_++;
    if (ema_tick_ >= 10 && ring_count_ > 0) {
        ema_tick_ = 0;
        // Average over recent samples
        float avg = 0;
        int n = ring_count_ < 10 ? ring_count_ : 10;
        for (int i = 0; i < n; i++) {
            int idx = (ring_idx_ - 1 - i + RING_SIZE) % RING_SIZE;
            avg += ring_[idx].flow;
        }
        avg /= static_cast<float>(n);

        uint64_t elapsed_us = time_.monotonic_us() - ema_start_us_;
        float elapsed_h = elapsed_us / 3600000000.0f;

        if (elapsed_h >= 1.0f / 60)  update_ema(ema_1h_,  avg, 0.01f);
        if (elapsed_h >= 3.0f)       update_ema(ema_3h_,  avg, 0.005f);
        if (elapsed_h >= 12.0f)      update_ema(ema_12h_, avg, 0.002f);
        if (elapsed_h >= 24.0f)      update_ema(ema_24h_, avg, 0.001f);
        if (elapsed_h >= 168.0f)     update_ema(ema_7d_,  avg, 0.0005f);
    }

    // Daily tracking only once wall clock is synced (NTP/manual)
    if (time_.is_synced()) {
        update_daily_tracking();
    }
}

void GasFlowService::update_daily_tracking()
{
    auto local_sec = std::chrono::duration_cast<std::chrono::seconds>(
        time_.local_now().time_since_epoch()).count();
    int64_t epoch_day = local_sec / 86400;

    if (today_epoch_day_ < 0) {
        today_epoch_day_ = epoch_day;
        daily_accumulator_ = 0;
        return;
    }

    if (epoch_day != today_epoch_day_) {
        // Day boundary: archive yesterday's consumption
        int idx = (daily_head_ + daily_count_) % DAILY_SLOTS;
        daily_[idx].epoch_day = today_epoch_day_;
        daily_[idx].m3 = daily_accumulator_;

        if (daily_count_ < DAILY_SLOTS - 1) {
            daily_count_++;
        } else {
            daily_head_ = (daily_head_ + 1) % DAILY_SLOTS;
        }

        today_epoch_day_ = epoch_day;
        daily_accumulator_ = 0;
    }
}

int GasFlowService::get_daily_view(DailyView* out, int max) const
{
    int n = 0;
    for (int i = 0; i < daily_count_ && n < max; i++) {
        const DailySlot& s = daily_[(daily_head_ + i) % DAILY_SLOTS];
        out[n].epoch_day = s.epoch_day;
        out[n].m3 = s.m3;
        n++;
    }
    if (n < max && today_epoch_day_ >= 0) {
        out[n].epoch_day = today_epoch_day_;
        out[n].m3 = daily_accumulator_;
        n++;
    }
    return n;
}

void GasFlowService::reset()
{
    integral_m3_ = 0;
    latest_flow_ = 0;
    ring_idx_ = 0;
    ring_count_ = 0;
    ema_1h_ = ema_3h_ = ema_12h_ = ema_24h_ = ema_7d_ = 0;
    ema_start_us_ = time_.monotonic_us();
    kalman_mod_.reset(0);
    kalman_ret_.reset(0);
    flame_prev_ = false;
    ignition_start_ms_ = 0;
    dhw_active_ = false;
    outdoor_temp_valid_ = false;
    outdoor_zero_start_ms_ = 0;
    flow_temp_valid_ = false;
    ret_temp_valid_  = false;
    daily_head_ = 0;
    daily_count_ = 0;
    today_epoch_day_ = -1;
    daily_accumulator_ = 0;
}
