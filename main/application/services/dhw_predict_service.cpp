#include "application/services/dhw_predict_service.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ipredict_store.h"
#include "application/ports/driven/itime_source.h"
#include <cmath>
#include <cstring>

// Kalman tuning constants
static constexpr float KALMAN_Q_TEMP = 0.0001f;
static constexpr float KALMAN_Q_RATE = 0.000005f;
static constexpr float KALMAN_R_MEAS = 0.25f;

// Prior defaults when no history
static constexpr float DEF_RATE_PRIOR = 0.030f;   // 0.03 °C/s ≈ 1.8 °C/min
static constexpr float DEF_RATE_VAR   = 0.001f;

// Clamps
static constexpr float MIN_RATE    = 0.002f;
static constexpr float MIN_VAR     = 1e-6f;
static constexpr float MIN_UNCERT  = 15.0f;

DHWPredictService::DHWPredictService(IHeatingStateStore& state, IPredictStore& store, ITimeSource& time)
    : state_(state), store_(store), time_(time)
{
    kalman_.reset(0, DEF_RATE_PRIOR);
}

void DHWPredictService::load_history()
{
    float rates[3] = {};
    int idx = 0, cnt = 0;
    if (store_.load_predict(rates, idx, cnt)) {
        memcpy(hist_rates_, rates, sizeof(float) * HIST_N);
        hist_idx_ = idx;
        hist_count_ = (cnt > HIST_N) ? HIST_N : cnt;
    }
}

uint32_t DHWPredictService::now_ms() const
{
    return static_cast<uint32_t>(time_.monotonic_ms());
}

// ── Session history ────────────────────────────────────────────

float DHWPredictService::hist_prior_rate() const
{
    if (hist_count_ == 0) return DEF_RATE_PRIOR;
    float sum = 0;
    for (int i = 0; i < hist_count_; i++) sum += hist_rates_[i];
    return sum / static_cast<float>(hist_count_);
}

float DHWPredictService::hist_prior_variance() const
{
    if (hist_count_ == 0) return DEF_RATE_VAR;
    float mean = hist_prior_rate();
    float sum = 0;
    for (int i = 0; i < hist_count_; i++) {
        float d = hist_rates_[i] - mean;
        sum += d * d;
    }
    float v = sum / static_cast<float>(hist_count_) + MIN_VAR;
    return (v < MIN_VAR) ? MIN_VAR : v;
}

// ── Session lifecycle ──────────────────────────────────────────

void DHWPredictService::start_session(float start_temp)
{
    float v0 = hist_prior_rate();
    kalman_.reset(start_temp, v0);

    session_start_temp_ = start_temp;
    session_start_ms_ = now_ms();
    last_update_ms_ = session_start_ms_;
    session_active_ = true;
    cycle_count_ = 0;

    state_.lock_exclusive();
    state_.set_dhw_prediction(false, 0, 0, 0, 0);
    state_.unlock_exclusive();
}

void DHWPredictService::update_session(float dhw_temp)
{
    if (!session_active_) return;

    uint32_t t = now_ms();
    float dt_s = static_cast<float>(t - last_update_ms_) / 1000.0f;
    last_update_ms_ = t;
    cycle_count_++;

    // Sanity: if gap too large, reset filter
    if (dt_s < 0.001f) dt_s = 1.0f;
    if (dt_s > 15.0f) {
        kalman_.reset(dhw_temp, kalman_.rate());
        dt_s = 1.0f;
    }

    // Current Kalman2D API: update(measurement, time_ms) handles predict+update internally
    float time_ms = static_cast<float>(session_start_ms_ + static_cast<uint32_t>((t - session_start_ms_)));
    kalman_.update(dhw_temp, time_ms);

    push_prediction();
}

void DHWPredictService::finish_session(uint32_t duration_ms)
{
    if (!session_active_) return;

    float actual_temp = kalman_.temperature();
    float dur_sec = static_cast<float>(duration_ms) / 1000.0f;

    // Compute actual heating rate
    float actual_rate = DEF_RATE_PRIOR;
    if (cycle_count_ > 2 && dur_sec > 10.0f && actual_temp > session_start_temp_) {
        actual_rate = (actual_temp - session_start_temp_) / dur_sec;
    }
    if (actual_rate < MIN_RATE) actual_rate = DEF_RATE_PRIOR;

    // Record in history ring buffer
    hist_rates_[hist_idx_] = actual_rate;
    hist_idx_ = (hist_idx_ + 1) % HIST_N;
    if (hist_count_ < HIST_N) hist_count_++;

    // Persist to NVS
    store_.save_predict(hist_rates_, hist_idx_, hist_count_);

    session_active_ = false;
    state_.lock_exclusive();
    state_.set_dhw_prediction(false, 0, 0, 0, 0);
    state_.unlock_exclusive();
}

void DHWPredictService::push_prediction()
{
    if (cycle_count_ < 2) return;

    float rate = kalman_.rate();
    float var_rate = kalman_.uncertainty(); // P11_ — variance of rate estimate
    float temp_now = kalman_.temperature();

    if (rate < MIN_RATE) rate = MIN_RATE;

    state_.lock_shared();
    float setpoint = state_.get_dhw_setpoint();
    state_.unlock_shared();

    float delta_t = setpoint - temp_now;
    if (delta_t < 0.5f) delta_t = 0.5f;

    uint32_t now = now_ms();
    float elapsed_s = static_cast<float>(now - session_start_ms_) / 1000.0f;
    if (elapsed_s < 0) elapsed_s = 0;

    float remaining_s = delta_t / rate;
    float sigma_rate = sqrtf(var_rate);
    float sigma_remaining = (delta_t / (rate * rate)) * sigma_rate;

    int remaining = static_cast<int>(remaining_s + 0.5f);
    if (remaining < 0) remaining = 0;
    int uncertainty = static_cast<int>(sigma_remaining + 0.5f);
    if (uncertainty < static_cast<int>(MIN_UNCERT)) uncertainty = static_cast<int>(MIN_UNCERT);
    int elapsed = static_cast<int>(elapsed_s + 0.5f);

    state_.lock_exclusive();
    state_.set_dhw_prediction(true, remaining, uncertainty, rate, elapsed);
    state_.unlock_exclusive();
}

// ── Main poll ──────────────────────────────────────────────────

void DHWPredictService::poll()
{
    state_.lock_shared();
    bool flame      = state_.is_flame_on();
    bool dhw_active = state_.is_dhw_active();
    float dhw_temp  = state_.get_dhw_temp();
    state_.unlock_shared();

    bool heating_dhw = flame && dhw_active;

    // Edge: DHW heating started
    if (heating_dhw && (!prev_flame_ || !prev_dhw_active_)) {
        start_session(dhw_temp);
    }

    // During session: update filter
    if (session_active_ && heating_dhw) {
        update_session(dhw_temp);
    }

    // Edge: DHW heating stopped
    if (session_active_ && !heating_dhw) {
        uint32_t now = now_ms();
        uint32_t dur = now - session_start_ms_;
        finish_session(dur);
        // Note: BoilerPollInteractor also calls set_dhw_session_finished() via do_dhw_hysteresis()
        // with its own min_temp tracking. Our finish_session() handles prediction state.
    }

    prev_flame_ = flame;
    prev_dhw_active_ = dhw_active;
}
