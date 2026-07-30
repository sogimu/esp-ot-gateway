#pragma once

#include <cstdint>
#include "application/ports/driving/icontrol_task.h"
#include "domain/services/kalman1d.h"

class IHeatingStateStore;
class ITimeSource;
class IHeatingStatsStore;

/// Estimates gas consumption from modulation % and return temperature.
/// Uses Kalman1D filters on raw inputs, physical model for flow rate,
/// and EMA accumulators for rolling averages.
class GasFlowService : public IControlTask {
public:
    static constexpr int RING_SIZE = 720; // 2h @ 10s interval

    GasFlowService(IHeatingStateStore& state, ITimeSource& time, IHeatingStatsStore& store);
    ~GasFlowService();

    void load_integral();  // restore integral_m3 from NVS

    void execute() override;
    void reset();

    // Accessors
    float instant_flow()     const { return latest_flow_; }
    float integral_m3()      const { return integral_m3_; }
    float avg_1h()           const { return ema_1h_; }
    float avg_3h()           const { return ema_3h_; }
    float avg_12h()          const { return ema_12h_; }
    float avg_24h()          const { return ema_24h_; }
    float avg_7d()           const { return ema_7d_; }
    float mod_filtered_val   = 0;
    float t_ret_filtered_val = 0;
    float mod_filtered()     const { return mod_filtered_val; }
    float t_ret_filtered()   const { return t_ret_filtered_val; }
    float k_calib()          const { return k_calib_; }
    void  set_k_calib(float v) { k_calib_ = v; }
    void  set_integral(float v) { integral_m3_ = v; }


    // EMA setters for NVS restore
    void set_ema_1h(float v)  { ema_1h_ = v; }
    void set_ema_3h(float v)  { ema_3h_ = v; }
    void set_ema_12h(float v) { ema_12h_ = v; }
    void set_ema_24h(float v) { ema_24h_ = v; }
    void set_ema_7d(float v)  { ema_7d_ = v; }
    uint64_t ema_start_us() const { return ema_start_us_; }
    void set_ema_start_us(uint64_t v) { ema_start_us_ = v; }

    // Public for testability — continuous efficiency curve vs return temp
    float efficiency_continuous(float t_ret) const;

private:
    /// Seasonal correction of calorific value based on outdoor temperature.
    /// Uses Tomsk nominal CV (9.45 kWh/m3) and Boyle's law:
    ///   T_gas = T_outdoor - 5C  (user's correction for buried gas pipe)
    ///   CV_eff = CV_nom * (15+273.15) / (T_gas+273.15)
    /// Falls back to gas_calorific_ if outdoor temperature is not yet received (outdoor_temp_valid_ == false).
    float corrected_calorific() const;
    float calc_power(float modulation_pct, float flow_temp, float ret_temp) const;
    IHeatingStateStore&  state_;
    ITimeSource&         time_;
    IHeatingStatsStore&  store_;

    Kalman1D kalman_mod_{0, 0.1f, 1.0f};
    Kalman1D kalman_ret_{0, 0.05f, 0.3f};

    float k_calib_ = 1.0f;
    float gas_calorific_ = 9.5f;
    float outdoor_temp_ = 0.0f;
    bool outdoor_temp_valid_ = false;
    float integral_m3_ = 0;
    float latest_flow_ = 0;

    // Flame-gating state
    bool flame_prev_ = false;
    uint32_t ignition_start_ms_ = 0;

    // DHW mode flag
    bool dhw_active_ = false;

    // Sensor validity flags (prevent ==0 sentinel ambiguity)
    bool flow_temp_valid_ = false;
    bool ret_temp_valid_  = false;

    // EMA ring + accumulators
    struct Sample { float flow; };
    Sample* ring_; // malloc'd
    int ring_idx_ = 0;
    int ring_count_ = 0;

    float ema_1h_ = 0, ema_3h_ = 0, ema_12h_ = 0, ema_24h_ = 0, ema_7d_ = 0;
    uint64_t ema_start_us_ = 0;

    uint32_t last_update_ms_ = 0;
    uint32_t outdoor_zero_start_ms_ = 0;
    int ema_tick_ = 0;

    void update_ema(float& ema, float val, float alpha);
};
