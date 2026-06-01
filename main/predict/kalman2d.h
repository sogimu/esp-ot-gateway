#pragma once

#include <cmath>

class Kalman2D {
public:
    Kalman2D() : Kalman2D(0.0001f, 0.000005f, 0.25f) {}
    Kalman2D(float q_temp, float q_rate, float r_meas)
        : R_(r_meas), q_{q_temp, q_rate}
    {
        reset(0, 0, 1e3f, 1e3f);
    }

    void predict(float dt) {
        float x0 = x_[0] + x_[1] * dt;
        x_[1] = x_[1];
        x_[0] = x0;

        float p00 = P_[0] + 2.0f * dt * P_[1] + dt * dt * P_[2] + q_[0] * dt;
        float p01 = P_[1] + dt * P_[2]                          + 0.0f;
        float p11 = P_[2]                                        + q_[1] * dt;
        P_[0] = p00;
        P_[1] = p01;
        P_[2] = p11;
    }

    void update(float measurement) {
        float innov = measurement - x_[0];
        float s = P_[0] + R_;
        float k0 = P_[0] / s;
        float k1 = P_[1] / s;

        x_[0] += k0 * innov;
        x_[1] += k1 * innov;

        float p00 = (1.0f - k0) * P_[0];
        float p01 = (1.0f - k0) * P_[1];
        float p11 = P_[2] - k1 * P_[1];
        P_[0] = p00;
        P_[1] = p01;
        P_[2] = p11;

        n_updates_++;
    }

    void reset(float t0, float v0, float p00, float p11) {
        x_[0] = t0;
        x_[1] = v0;
        P_[0] = p00;
        P_[1] = 0.0f;
        P_[2] = p11;
        n_updates_ = 0;
    }

    float temp() const  { return x_[0]; }
    float rate() const  { return x_[1]; }
    float var_temp() const { return P_[0]; }
    float var_rate() const { return P_[2]; }
    int   updates() const { return n_updates_; }

private:
    float x_[2];
    float P_[3];
    float R_;
    float q_[2];
    int   n_updates_ = 0;
};
