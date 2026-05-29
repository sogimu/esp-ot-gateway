#pragma once

#include "model/model.h"
#include "interfaces/iopentherm_observer.h"
#include "endpoints/opentherm/opentherm_endpoint.h"

#include <cstdint>
#include "nvs.h"
#include "nvs_flash.h"

#define HIST_BINS 1000
#define CYCLE_RING 256

// Gas flow history ring buffer for 1h sliding average at ~10s interval
#define GAS_RING_SIZE 720

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

class GasFlowEstimator {
public:
    GasFlowEstimator();
    void set_params(float p_max_kw, float gas_cal);
    void set_k_calib(float v);
    float get_k_calib() const;

    void update(float mod_raw, float t_ret_raw, uint32_t dt_ms);
    void push_to_model(Model& model);

private:
    static float eta_corr(float t_ret);

    float flow_max_;
    float k_calib_;
    float integral_m3_;

    Kalman1D kalman_mod_;
    Kalman1D kalman_ret_;

    float latest_flow_;
    float mod_filt_;
    float t_ret_filt_;

    // Ring buffer for ~2h of flow samples (720 @ 10s)
    struct GasSample {
        float flow;
    } ring_[GAS_RING_SIZE];
    int ring_idx_;
    int ring_count_;

    // EMA accumulators for long windows
    float ema_1h_;
    float ema_3h_;
    float ema_12h_;
    float ema_24h_;
    float ema_7d_;
    uint64_t ema_start_us_;
};

class StatsService : public IOpenthermObserver {
public:
    StatsService(Model& model, OpenthermEndpoint& ot);

    void start();
    void stop();

    GasFlowEstimator& gas() { return gas_; }
    void load_nvs_meter();
    void save_nvs_meter();

    // IOpenthermObserver
    void on_connected() override {}
    void on_disconnected() override {}
    void on_status_changed(bool fault, bool flame, bool ch_active, bool dhw_active) override;
    void on_ch_temp(float value) override { (void)value; }
    void on_dhw_temp(float value) override { (void)value; }
    void on_return_temp(float value) override;
    void on_outside_temp(float value) override { (void)value; }
    void on_modulation(float pct) override;
    void on_ch_bounds(float min, float max) override { (void)min; (void)max; }
    void on_dhw_bounds(float min, float max) override { (void)min; (void)max; }
    void on_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) override { (void)asf; (void)oem_fault; (void)oem_diag; }
    void on_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) override { (void)bs; (void)cps; (void)dvs; (void)dbs; }
    void on_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) override { (void)bh; (void)cph; (void)dvh; (void)dbh; }
    void on_version(uint8_t st, uint8_t sv, float ov) override { (void)st; (void)sv; (void)ov; }
    void on_dhw_session_finished(uint32_t dur_ms, float min_temp) override { (void)dur_ms; (void)min_temp; }

private:
    void push_stats();
    void try_gas_estimate();

    Model& model_;
    OpenthermEndpoint& ot_;
    bool started_ = false;

    uint16_t hist_[HIST_BINS];
    uint32_t samples_;

    uint16_t burn_dur_[CYCLE_RING];
    uint16_t pause_dur_[CYCLE_RING];
    int cycle_idx_;
    int cycle_total_;
    uint32_t burner_sec_;
    uint32_t cycle_cnt_;
    uint32_t flame_on_ms_;
    uint32_t flame_off_ms_;

    GasFlowEstimator gas_;
    float latest_mod_raw_ = 0;
    float latest_ret_raw_ = 0;
    uint32_t last_gas_ms_ = 0;
};