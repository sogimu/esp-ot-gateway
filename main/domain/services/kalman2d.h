#pragma once

/// 2D Kalman filter (temperature + rate of change).
/// Header-only — no heap, no dependencies.
class Kalman2D {
public:
    Kalman2D() { reset(0, 0.03f); }

    /// Initialize/reset with prior temperature and prior rate.
    void reset(float temp, float prior_rate) {
        x0_ = temp;
        x1_ = prior_rate;
        P00_ = 1e3f;
        P01_ = 0;
        P11_ = 1e3f;
        last_time_ms_ = 0;
        initialized_ = true;
    }

    /// Process a new measurement. Handles predict+update in one call.
    /// time_ms: absolute time in milliseconds since boot.
    /// Returns the filtered temperature estimate.
    float update(float measurement, float time_ms) {
        if (!initialized_) {
            reset(measurement, x1_);
            last_time_ms_ = time_ms;
            return measurement;
        }

        float dt = (time_ms - last_time_ms_) / 1000.0f;
        last_time_ms_ = time_ms;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 60.0f) dt = 60.0f;

        // Predict step
        float x0_pred = x0_ + x1_ * dt;
        float p00_pred = P00_ + 2.0f * dt * P01_ + dt * dt * P11_ + Q0_ * dt;
        float p01_pred = P01_ + dt * P11_;
        float p11_pred = P11_ + Q1_ * dt;

        // Update step
        float innov = measurement - x0_pred;
        float s = p00_pred + R_;
        float k0 = p00_pred / s;
        float k1 = p01_pred / s;

        x0_ = x0_pred + k0 * innov;
        x1_ = x1_ + k1 * innov;
        P00_ = (1.0f - k0) * p00_pred;
        P01_ = (1.0f - k0) * p01_pred;
        P11_ = p11_pred - k1 * p01_pred;

        return x0_;
    }

    /// Predicted temperature at future time_ms (from last update).
    float predict(float future_time_ms) const {
        float dt = (future_time_ms - last_time_ms_) / 1000.0f;
        return x0_ + x1_ * dt;
    }

    float temperature() const { return x0_; }
    float rate()        const { return x1_; }
    float uncertainty() const { return P11_; }

private:
    float x0_ = 0, x1_ = 0.03f;
    float P00_ = 1e3f, P01_ = 0, P11_ = 1e3f;
    float Q0_ = 0.0001f, Q1_ = 0.000005f;
    float R_ = 0.25f;
    float last_time_ms_ = 0;
    bool  initialized_ = false;
};
