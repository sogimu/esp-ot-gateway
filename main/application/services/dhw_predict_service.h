#pragma once

#include <cstdint>
#include "application/ports/driving/ipollable.h"
#include "domain/services/kalman2d.h"

class IHeatingStateStore;
class IConfigurationStore;
class ITimeSource;

/// DHW heating time prediction using Kalman2D filter.
/// Tracks temperature rise rate during DHW sessions and predicts
/// remaining time to reach setpoint.
///
/// Thread safety: poll() called from main_poll task only.
/// Reads from state_ (lock_shared), writes prediction to state_ (lock_exclusive).
class DHWPredictService : public IPollable {
public:
    DHWPredictService(IHeatingStateStore& state, IConfigurationStore& config, ITimeSource& time);

    void poll() override;

    // NVS persistence — called after construction to restore saved state
    void load_history();

private:
    IHeatingStateStore&  state_;
    IConfigurationStore& config_;
    ITimeSource&         time_;

    // Kalman filter state
    Kalman2D kalman_;
    bool     session_active_ = false;
    float    session_start_temp_ = 0;
    uint32_t session_start_ms_ = 0;
    uint32_t last_update_ms_ = 0;
    int      cycle_count_ = 0;

    // Session history (3-entry ring buffer for rate priors)
    static constexpr int HIST_N = 3;
    float hist_rates_[HIST_N] = {};
    int   hist_idx_ = 0;
    int   hist_count_ = 0;

    // Previous DHW state for edge detection
    bool prev_flame_ = false;
    bool prev_dhw_active_ = false;

    // Helpers
    void start_session(float start_temp);
    void update_session(float dhw_temp);
    void finish_session(uint32_t duration_ms);
    void push_prediction();
    float hist_prior_rate() const;
    float hist_prior_variance() const;

    uint32_t now_ms() const;
};
