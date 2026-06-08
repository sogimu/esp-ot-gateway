#pragma once

/// 1D Kalman filter — extracted from old stats_service.h.
class Kalman1D {
public:
    Kalman1D(float init, float q, float r)
        : x_(init), P_(1.0f), Q_(q), R_(r) {}

    float update(float measurement) {
        P_ += Q_;
        float K = P_ / (P_ + R_);
        x_ += K * (measurement - x_);
        P_ *= (1.0f - K);
        return x_;
    }

    void reset(float init) {
        x_ = init;
        P_ = 1.0f;
    }

private:
    float x_, P_, Q_, R_;
};
