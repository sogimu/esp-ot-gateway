#include "application/services/gas_flow_estimator.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

GasFlowService::GasFlowService(IHeatingStateStore& state, ITimeSource& time)
    : state_(state), time_(time)
{
    ring_ = static_cast<Sample*>(malloc(RING_SIZE * sizeof(Sample)));
    if (ring_) std::memset(ring_, 0, RING_SIZE * sizeof(Sample));
}

GasFlowService::~GasFlowService()
{
    free(ring_);
}

float GasFlowService::efficiency_correction(float t_ret)
{
    // Baxi Duo-tec efficiency curve vs return temp
    if (t_ret <= 30) return 0.98f;
    if (t_ret <= 40) return 0.96f;
    if (t_ret <= 50) return 0.94f;
    if (t_ret <= 60) return 0.91f;
    return 0.88f;
}

void GasFlowService::update_ema(float& ema, float val, float alpha)
{
    if (ema == 0) ema = val;
    else ema = alpha * val + (1.0f - alpha) * ema;
}

void GasFlowService::poll()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.now_us() / 1000);
    if (last_update_ms_ == 0) {
        last_update_ms_ = now_ms;
        ema_start_us_ = time_.now_us();
        return;
    }

    state_.lock_shared();
    float mod_raw = state_.get_modulation();
    float t_ret   = state_.get_return_temp();
    float p_max   = state_.get_p_max();
    float gas_cal = state_.get_gas_calorific();
    state_.unlock_shared();

    if (p_max > 0) p_max_kw_ = p_max;
    if (gas_cal > 0) gas_calorific_ = gas_cal;

    // Kalman filter raw inputs
    float mod_f = kalman_mod_.update(mod_raw);
    float ret_f = kalman_ret_.update(t_ret);
    mod_filtered_val = mod_f;
    t_ret_filtered_val = ret_f;

    // Physical model: flow [m3/h] = k * (mod%/100) * (Pmax[kW] / gas_cal[kWh/m3]) * eta(Tret)
    float eta = efficiency_correction(ret_f);
    float flow = k_calib_ * (mod_f / 100.0f) * (p_max_kw_ / gas_calorific_) * eta;
    if (flow < 0) flow = 0;
    latest_flow_ = flow;

    // Integrate flow (trapezoidal)
    uint32_t dt_ms = now_ms - last_update_ms_;
    if (dt_ms > 0 && dt_ms < 60000) {
        float dt_h = static_cast<float>(dt_ms) / 3600000.0f;
        integral_m3_ += flow * dt_h;
    }
    last_update_ms_ = now_ms;

    // Ring buffer for sliding window EMA
    ring_[ring_idx_].flow = flow;
    ring_idx_ = (ring_idx_ + 1) % RING_SIZE;
    if (ring_count_ < RING_SIZE) ring_count_++;

    // Compute EMAs every ~10 samples
    static int ema_tick = 0;
    ema_tick++;
    if (ema_tick >= 10 && ring_count_ > 0) {
        ema_tick = 0;
        // Average over recent samples
        float avg = 0;
        int n = ring_count_ < 10 ? ring_count_ : 10;
        for (int i = 0; i < n; i++) {
            int idx = (ring_idx_ - 1 - i + RING_SIZE) % RING_SIZE;
            avg += ring_[idx].flow;
        }
        avg /= static_cast<float>(n);

        uint64_t elapsed_us = time_.now_us() - ema_start_us_;
        float elapsed_h = elapsed_us / 3600000000.0f;

        if (elapsed_h >= 1.0f / 60)  update_ema(ema_1h_,  avg, 0.01f);
        if (elapsed_h >= 3.0f)       update_ema(ema_3h_,  avg, 0.005f);
        if (elapsed_h >= 12.0f)      update_ema(ema_12h_, avg, 0.002f);
        if (elapsed_h >= 24.0f)      update_ema(ema_24h_, avg, 0.001f);
        if (elapsed_h >= 168.0f)     update_ema(ema_7d_,  avg, 0.0005f);
    }
}

void GasFlowService::reset()
{
    integral_m3_ = 0;
    latest_flow_ = 0;
    ring_idx_ = 0;
    ring_count_ = 0;
    ema_1h_ = ema_3h_ = ema_12h_ = ema_24h_ = ema_7d_ = 0;
    ema_start_us_ = time_.now_us();
    kalman_mod_.reset(0);
    kalman_ret_.reset(0);
}
