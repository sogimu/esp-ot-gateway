#pragma once

#include <stdint.h>
#include "interfaces/iopentherm_observer.h"
#include "model/model.h"
#include "kalman2d.h"

class OpenthermEndpoint;
class ConfigEndpoint;

class PredictService : public IOpenthermObserver {
public:
    PredictService(Model& model);

    void start(OpenthermEndpoint& ot, ConfigEndpoint& config);

    struct PredResult {
        bool active = false;
        int  elapsed_sec = 0;
        int  remaining_sec = 0;
        int  uncertainty_sec = 0;
        float rate_cps = 0;
    };

    PredResult get_dhw_prediction() const { return dhw_result_; }

private:
    struct SessionHistory {
        static constexpr int N = 3;
        float rates_[N] = {};
        int   idx_ = 0;
        int   count_ = 0;

        void record(float rate);
        float prior_rate() const;
        float prior_variance() const;
    };

    struct DhwPred {
        Kalman2D kalman;
        SessionHistory history;
        float start_temp = 0;
        float start_ms = 0;
        float last_update_ms = 0;
        float setpoint = 55.0f;
        bool  active = false;
        int   cycle_count = 0;
    };

    Model& model_;
    ConfigEndpoint* config_ = nullptr;
    DhwPred dhw_;
    PredResult dhw_result_;

    void push_prediction();
    float now_ms() const;

    void on_connected() override {}
    void on_disconnected() override {}
    void on_status_changed(bool, bool, bool, bool) override {}
    void on_ch_temp(float) override {}
    void on_dhw_temp(float value) override;
    void on_return_temp(float) override {}
    void on_outside_temp(float) override {}
    void on_modulation(float) override {}
    void on_ch_bounds(float,float) override {}
    void on_dhw_bounds(float,float) override {}
    void on_fault_codes(uint8_t,uint8_t,uint16_t) override {}
    void on_runtime_counters(uint16_t,uint16_t,uint16_t,uint16_t) override {}
    void on_runtime_hours(uint16_t,uint16_t,uint16_t,uint16_t) override {}
    void on_version(uint8_t,uint8_t,float) override {}
    void on_dhw_session_finished(uint32_t duration_ms, float min_temp) override;
    void on_ch_setpoint_confirmed(float) override {}
    void on_dhw_setpoint_confirmed(float value) override;
    void on_dhw_session_started(float start_temp) override;
};
