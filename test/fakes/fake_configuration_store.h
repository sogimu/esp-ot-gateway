#pragma once

#include "application/ports/driven/iconfiguration_store.h"
#include <cstring>

/// Fake configuration store — in-memory, for unit testing.
class FakeConfigurationStore : public IConfigurationStore {
public:
    FakeConfigurationStore() { clear(); }

    void clear() {
        save_config_called_ = 0;
        save_predict_called_ = 0;
        std::memset(saved_rates_, 0, sizeof(saved_rates_));
        saved_idx_ = 0;
        saved_count_ = 0;
        predict_returns_ = false;
        std::memset(predict_rates_, 0, sizeof(predict_rates_));
        predict_idx_ = 0;
        predict_count_ = 0;
    }

    void load_all(class IHeatingStateStore&) override {}

    void save_config(const class IHeatingStateStore&) override {
        save_config_called_++;
    }

    void save_stats(const class IHeatingStateStore&,
                    uint32_t, float,
                    const void*, const void*,
                    const void*, const void*) override {}

    bool load_stats(uint32_t&, float&,
                    void*, void*,
                    void*, void*) override { return false; }

    void save_burner_sec(uint32_t burner_sec, uint32_t cycle_cnt) override {
        saved_burner_sec_ = burner_sec;
        saved_cycle_cnt_ = cycle_cnt;
        save_burner_sec_called_ = true;
    }
    bool load_burner_sec(uint32_t& burner_sec, uint32_t& cycle_cnt) override {
        if (!load_burner_sec_returns_) return false;
        burner_sec = saved_burner_sec_;
        cycle_cnt = saved_cycle_cnt_;
        return true;
    }
    void set_burner_sec_load(uint32_t bs, uint32_t cc) {
        load_burner_sec_returns_ = true;
        saved_burner_sec_ = bs;
        saved_cycle_cnt_ = cc;
    }

    void save_meter(const class IHeatingStateStore&) override {}
    bool load_meter(class IHeatingStateStore&) override { return false; }

    void save_predict(const float rates[3], int idx, int count) override {
        save_predict_called_++;
        std::memcpy(saved_rates_, rates, 3 * sizeof(float));
        saved_idx_ = idx;
        saved_count_ = count;
    }

    bool load_predict(float rates[3], int& idx, int& count) override {
        if (!predict_returns_) return false;
        std::memcpy(rates, predict_rates_, 3 * sizeof(float));
        idx = predict_idx_;
        count = predict_count_;
        return true;
    }

    /// Configure what load_predict returns.
    void set_predict_history(const float rates[3], int idx, int count) {
        predict_returns_ = true;
        std::memcpy(predict_rates_, rates, 3 * sizeof(float));
        predict_idx_ = idx;
        predict_count_ = count;
    }

    // ── Call tracking for test assertions ─────────────────
    int save_config_called_ = 0;
    int save_predict_called_ = 0;
    float saved_rates_[3] = {};
    int saved_idx_ = 0;
    int saved_count_ = 0;
    bool save_burner_sec_called_ = false;
    uint32_t saved_burner_sec_ = 0;
    uint32_t saved_cycle_cnt_ = 0;
    bool load_burner_sec_returns_ = false;

private:
    bool predict_returns_ = false;
    float predict_rates_[3] = {};
    int predict_idx_ = 0;
    int predict_count_ = 0;
};
