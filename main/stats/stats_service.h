#pragma once

#include "model/model.h"
#include "interfaces/iopentherm_observer.h"
#include "endpoints/opentherm/opentherm_endpoint.h"

#include <cstdint>

#define HIST_BINS 1000
#define CYCLE_RING 256

class StatsService : public IOpenthermObserver {
public:
    StatsService(Model& model, OpenthermEndpoint& ot);

    void start();
    void stop();

    // IOpenthermObserver
    void on_connected() override {}
    void on_disconnected() override {}
    void on_status_changed(bool fault, bool flame, bool ch_active, bool dhw_active) override;
    void on_ch_temp(float value) override { (void)value; }
    void on_dhw_temp(float value) override { (void)value; }
    void on_return_temp(float value) override { (void)value; }
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
};