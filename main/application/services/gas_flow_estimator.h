#pragma once

#include <cstdint>
#include "application/ports/driving/icontrol_task.h"
#include "application/ports/driven/igas_correction_store.h"
#include "domain/services/kalman1d.h"

class IHeatingStateStore;
class ITimeSource;
class IHeatingStatsStore;
class IGasCorrectionStore;

/// Estimates gas consumption from modulation % and return temperature.
/// Uses Kalman1D filters on raw inputs and a physical model for flow rate.
class GasFlowService : public IControlTask {
public:
    static constexpr int DAILY_SLOTS = 64;    // ~2 месяца завершённых суток
    static constexpr int HOURS_PER_DAY = 24;

    /// One entry of the gas consumption chart (for web rendering).
    struct DailyView {
        int64_t epoch_day;   // local day number (local_now().time_since_epoch() / 86400)
        float   m3;          // gas consumed that day, m3
    };
    struct HourlyView {
        int64_t epoch_hour;  // local hour number (local_now().time_since_epoch() / 3600)
        float   m3;          // gas consumed that hour, m3
    };

    GasFlowService(IHeatingStateStore& state, ITimeSource& time, IHeatingStatsStore& store,
                   IGasCorrectionStore& gas_store);

    void load_integral();  // restore integral_m3 from NVS

    void execute() override;
    void reset();

    // Accessors
    float instant_flow()     const { return latest_flow_; }
    float integral_m3()      const { return integral_m3_; }
    float mod_filtered_val   = 0;
    float t_ret_filtered_val = 0;
    float mod_filtered()     const { return mod_filtered_val; }
    float t_ret_filtered()   const { return t_ret_filtered_val; }
    float k_calib()          const { return k_calib_; }
    void  set_k_calib(float v) { k_calib_ = v; }
    void  set_integral(float v) { integral_m3_ = v; }

    // Public for testability — continuous efficiency curve vs return temp
    float efficiency_continuous(float t_ret) const;

    // ── Daily/hourly consumption tracking (NVS-persisted via IGasCorrectionStore) ──
    void update_daily_tracking();
    void update_hourly_tracking();
    int  get_daily_view(DailyView* out, int max) const;
    int  get_hourly_view(HourlyView* out, int max) const;
    int64_t today_epoch_day() const { return today_epoch_day_; }
    void load_daily();
    void pack_today(GasTodayBlob& blob) const;
    void pack_history(GasHistoryBlob& blob) const;
    bool consume_history_dirty();

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
    IGasCorrectionStore& gas_store_;

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

    uint32_t last_update_ms_ = 0;
    uint32_t outdoor_zero_start_ms_ = 0;

    // ── Daily/hourly consumption tracking ──
    // daily_accumulator_ runs in parallel with integral_m3_ but is NOT reset
    // by corrections (set_integral(0) doesn't touch it). Reset only at day
    // boundary and on explicit stats reset.
    GasDailyEntry daily_[DAILY_SLOTS];
    int       daily_head_  = 0;
    int       daily_count_ = 0;
    int64_t   today_epoch_day_ = -1;   // -1 = not initialized (wall clock not synced yet)
    float     daily_accumulator_ = 0;
    bool      history_dirty_ = false;

    // Hourly buckets: completed hours of today, current (partial) hour and
    // yesterday's completed hours. Hour/day arrays are zero-filled, so missing
    // buckets read as 0.
    int64_t   today_epoch_hour_ = -1;  // -1 = not initialized
    float     today_hours_[HOURS_PER_DAY] = {};
    float     hourly_accumulator_ = 0;
    int64_t   yesterday_epoch_day_ = -1;
    float     yesterday_hours_[HOURS_PER_DAY] = {};

    void push_daily(int64_t epoch_day, float m3);
};
