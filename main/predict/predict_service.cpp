#include "predict/predict_service.h"
#include "endpoints/opentherm/opentherm_endpoint.h"
#include "endpoints/config/config_endpoint.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>

static constexpr float KALMAN_Q_TEMP = 0.0001f;
static constexpr float KALMAN_Q_RATE = 0.000005f;
static constexpr float KALMAN_R_MEAS = 0.25f;

static constexpr float DEF_RATE_PRIOR = 0.030f;
static constexpr float DEF_RATE_VAR   = 0.001f;

static constexpr float MIN_RATE  = 0.002f;
static constexpr float MIN_VAR   = 1e-6f;
static constexpr float MIN_UNCERT = 15.0f;

PredictService::PredictService(Model& model)
    : model_(model)
{}

void PredictService::start(OpenthermEndpoint& ot, ConfigEndpoint& config)
{
    ot.subscribe(this);
    config_ = &config;

    float rates[3] = {};
    int idx = 0, count = 0;
    if (config_->load_predict(rates, idx, count)) {
        auto& h = dhw_.history;
        memcpy(h.rates_, rates, sizeof(float) * 3);
        h.idx_ = idx;
        h.count_ = count;
        if (h.count_ > 3) h.count_ = 3;
    }
}

float PredictService::now_ms() const
{
    return (float)(esp_timer_get_time() / 1000LL);
}

void PredictService::SessionHistory::record(float rate)
{
    rates_[idx_] = rate;
    idx_ = (idx_ + 1) % N;
    if (count_ < N) count_++;
}

float PredictService::SessionHistory::prior_rate() const
{
    if (count_ == 0) return DEF_RATE_PRIOR;
    float sum = 0;
    int n = count_;
    for (int i = 0; i < n; i++) sum += rates_[i];
    return sum / (float)n;
}

float PredictService::SessionHistory::prior_variance() const
{
    if (count_ == 0) return DEF_RATE_VAR;
    float mean = prior_rate();
    float sum = 0;
    int n = count_;
    for (int i = 0; i < n; i++) {
        float d = rates_[i] - mean;
        sum += d * d;
    }
    float v = sum / (float)n + MIN_VAR;
    if (v < MIN_VAR) v = MIN_VAR;
    return v;
}

void PredictService::on_dhw_session_started(float start_temp)
{
    auto& d = dhw_;
    float v0 = d.history.prior_rate();
    float vv = d.history.prior_variance();

    d.kalman.reset(start_temp, v0, KALMAN_R_MEAS, vv);
    d.start_temp = start_temp;
    d.start_ms = now_ms();
    d.last_update_ms = d.start_ms;
    d.setpoint = 55.0f;
    d.active = true;
    d.cycle_count = 0;

    dhw_result_ = PredResult{};
}

void PredictService::on_dhw_temp(float value)
{
    auto& d = dhw_;
    if (!d.active) return;

    float t = now_ms();
    float dt_s = (t - d.last_update_ms) / 1000.0f;
    d.last_update_ms = t;
    d.cycle_count++;

    if (dt_s < 0.001f) dt_s = 1.0f;
    if (dt_s > 15.0f) {
        d.kalman.reset(value, d.kalman.rate(), KALMAN_R_MEAS, d.kalman.var_rate());
        dt_s = 1.0f;
    }

    d.kalman.predict(dt_s);
    d.kalman.update(value);

    push_prediction();
}

void PredictService::on_dhw_session_finished(uint32_t duration_ms, float min_temp)
{
    (void)min_temp;
    auto& d = dhw_;
    if (!d.active) return;

    float n_up = (float)d.kalman.updates();
    float actual_temp = d.kalman.temp();

    float dur_sec = (float)duration_ms / 1000.0f;
    float actual_rate = (n_up > 2.0f && dur_sec > 10.0f && actual_temp > d.start_temp)
        ? (actual_temp - d.start_temp) / dur_sec
        : DEF_RATE_PRIOR;
    if (actual_rate < MIN_RATE) actual_rate = DEF_RATE_PRIOR;

    d.history.record(actual_rate);
    if (config_) {
        config_->save_predict(d.history.rates_, d.history.idx_, d.history.count_);
    }
    d.active = false;
    dhw_result_.active = false;
    dhw_result_.remaining_sec = 0;
    dhw_result_.uncertainty_sec = 0;

    model_.set_dhw_prediction(false, 0, 0, 0, 0);
}

void PredictService::on_dhw_setpoint_confirmed(float value)
{
    dhw_.setpoint = value;
}

void PredictService::push_prediction()
{
    auto& d = dhw_;
    auto& r = dhw_result_;

    if (d.kalman.updates() < 2) return;

    float rate = d.kalman.rate();
    float var_rate = d.kalman.var_rate();
    float temp_now = d.kalman.temp();

    if (rate < MIN_RATE) rate = MIN_RATE;

    float delta_t = d.setpoint - temp_now;
    if (delta_t < 0.5f) delta_t = 0.5f;

    float elapsed_s = (now_ms() - d.start_ms) / 1000.0f;
    if (elapsed_s < 0) elapsed_s = 0;

    float remaining_s = delta_t / rate;
    float sigma_rate = sqrtf(var_rate);
    float sigma_remaining = (delta_t / (rate * rate)) * sigma_rate;

    int remaining = (int)(remaining_s + 0.5f);
    if (remaining < 0) remaining = 0;
    int uncertainty = (int)(sigma_remaining + 0.5f);
    if (uncertainty < (int)MIN_UNCERT) uncertainty = (int)MIN_UNCERT;

    r.active = true;
    r.elapsed_sec = (int)(elapsed_s + 0.5f);
    r.remaining_sec = remaining;
    r.uncertainty_sec = uncertainty;
    r.rate_cps = rate;

    model_.set_dhw_prediction(true, remaining, uncertainty, rate, r.elapsed_sec);
}
