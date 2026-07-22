#pragma once

#include "application/ports/driven/iconfiguration_store.h"
#include "application/ports/driven/iheating_state_store.h"
#include "nvs_config_store.h"  // NvsMeterBlob
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
        burn_stats_saved_ = false;
        std::memset(&saved_burn_, 0, sizeof(saved_burn_));
        burn_stats_load_returns_ = false;
        std::memset(&load_burn_, 0, sizeof(load_burn_));
        total_uptime_saved_ = 0;
        total_uptime_load_ = 0;
        total_uptime_load_ok_ = false;
        // Stats blob nullptr tracking
        stats_save_h_null_ = false;
        stats_save_c_null_ = false;
        stats_save_e_null_ = false;
        stats_save_cal_null_ = false;
        stats_load_h_null_ = false;
        stats_load_c_null_ = false;
        stats_load_e_null_ = false;
        stats_load_cal_null_ = false;
        stats_save_bs_ = 0xFFFFFFFF;
        stats_save_integ_ = -1.0f;
        meter_save_called_ = false;
        meter_load_called_ = false;
        meter_load_returns_ = false;
        std::memset(&saved_meter_blob_, 0, sizeof(saved_meter_blob_));
    }

    void load_all(class IHeatingStateStore&) {}

    void save_config(const class IHeatingStateStore&) {
        save_config_called_++;
    }

    // save_stats/load_stats — regular methods (no longer in IConfigurationStore,
    // but kept for test compatibility with test_nvs_nullptr_safety).
    void save_stats(const class IHeatingStateStore&, uint32_t bs, float integ_m3,
                    const void* h, const void* c, const void* e, const void* cal) {
        stats_save_bs_ = bs;
        stats_save_integ_ = integ_m3;
        stats_save_h_null_   = (h == nullptr);
        stats_save_c_null_   = (c == nullptr);
        stats_save_e_null_   = (e == nullptr);
        stats_save_cal_null_ = (cal == nullptr);
    }
    bool load_stats(uint32_t& bs, float& integ_m3,
                    void* h, void* c, void* e, void* cal) {
        stats_load_h_null_   = (h == nullptr);
        stats_load_c_null_   = (c == nullptr);
        stats_load_e_null_   = (e == nullptr);
        stats_load_cal_null_ = (cal == nullptr);
        bs = stats_save_bs_;
        integ_m3 = stats_save_integ_;
        return true;
    }

    void save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
                         uint32_t inter_pause_sec, uint32_t inter_cnt,
                         uint32_t mod_pause_sec, uint32_t mod_cnt) {
        burn_stats_saved_ = true;
        saved_burn_.burner_sec = burner_sec;
        saved_burn_.total_pause_sec = total_pause_sec;
        saved_burn_.cycle_cnt = cycle_cnt;
        saved_burn_.inter_pause_sec = inter_pause_sec;
        saved_burn_.inter_cnt = inter_cnt;
        saved_burn_.mod_pause_sec = mod_pause_sec;
        saved_burn_.mod_cnt = mod_cnt;
    }

    bool load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
                         uint32_t& inter_pause_sec, uint32_t& inter_cnt,
                         uint32_t& mod_pause_sec, uint32_t& mod_cnt) {
        if (!burn_stats_load_returns_) return false;
        burner_sec = load_burn_.burner_sec;
        total_pause_sec = load_burn_.total_pause_sec;
        cycle_cnt = load_burn_.cycle_cnt;
        inter_pause_sec = load_burn_.inter_pause_sec;
        inter_cnt = load_burn_.inter_cnt;
        mod_pause_sec = load_burn_.mod_pause_sec;
        mod_cnt = load_burn_.mod_cnt;
        return true;
    }

    void set_burn_stats_load(uint32_t bs, uint32_t tps, uint32_t cc,
                             uint32_t ips, uint32_t ic, uint32_t mps, uint32_t mc) {
        burn_stats_load_returns_ = true;
        load_burn_.burner_sec = bs;
        load_burn_.total_pause_sec = tps;
        load_burn_.cycle_cnt = cc;
        load_burn_.inter_pause_sec = ips;
        load_burn_.inter_cnt = ic;
        load_burn_.mod_pause_sec = mps;
        load_burn_.mod_cnt = mc;
    }

    void save_meter(const class IHeatingStateStore& s, const void* blob = nullptr) {
        meter_save_called_ = true;
        if (blob) {
            std::memcpy(&saved_meter_blob_, blob, sizeof(NvsMeterBlob));
        } else {
            std::memset(&saved_meter_blob_, 0, sizeof(saved_meter_blob_));
            saved_meter_blob_.base_reading = s.get_gas_meter_base();
        }
    }
    bool load_meter(class IHeatingStateStore& s, void* blob = nullptr) {
        meter_load_called_ = true;
        if (!meter_load_returns_) return false;
        s.set_gas_meter_base(meter_load_base_);
        if (blob) std::memcpy(blob, &meter_load_blob_, sizeof(NvsMeterBlob));
        return true;
    }
    void save_integral(float value) { saved_integral_ = value; save_integral_called_ = true; }

    void save_total_uptime(uint32_t sec) { total_uptime_saved_ = sec; }
    bool load_total_uptime(uint32_t& sec) {
        if (total_uptime_load_ok_) { sec = total_uptime_load_; return true; }
        return false;
    }

    void set_meter_load(float base, const NvsMeterBlob* blob = nullptr) {
        meter_load_returns_ = true;
        meter_load_base_ = base;
        if (blob) std::memcpy(&meter_load_blob_, blob, sizeof(NvsMeterBlob));
        else std::memset(&meter_load_blob_, 0, sizeof(meter_load_blob_));
    }
    float meter_load_base_ = 0;
    NvsMeterBlob meter_load_blob_;

    void save_predict(const float rates[3], int idx, int count) {
        save_predict_called_++;
        std::memcpy(saved_rates_, rates, 3 * sizeof(float));
        saved_idx_ = idx;
        saved_count_ = count;
    }

    bool load_predict(float rates[3], int& idx, int& count) {
        if (!predict_returns_) return false;
        std::memcpy(rates, predict_rates_, 3 * sizeof(float));
        idx = predict_idx_;
        count = predict_count_;
        return true;
    }

    void set_predict_history(const float rates[3], int idx, int count) {
        predict_returns_ = true;
        std::memcpy(predict_rates_, rates, 3 * sizeof(float));
        predict_idx_ = idx;
        predict_count_ = count;
    }

    void set_total_uptime_load(uint32_t sec) { total_uptime_load_ok_ = true; total_uptime_load_ = sec; }

    float saved_integral_ = -1.0f;
    bool save_integral_called_ = false;

    // ── Call tracking ──────────────────────────────────
    int save_config_called_ = 0;
    int save_predict_called_ = 0;
    float saved_rates_[3] = {};
    int saved_idx_ = 0;
    int saved_count_ = 0;

    // Burn stats
    bool burn_stats_saved_ = false;
    struct { uint32_t burner_sec, total_pause_sec, cycle_cnt, inter_pause_sec, inter_cnt, mod_pause_sec, mod_cnt; } saved_burn_;
    bool burn_stats_load_returns_ = false;
    struct { uint32_t burner_sec, total_pause_sec, cycle_cnt, inter_pause_sec, inter_cnt, mod_pause_sec, mod_cnt; } load_burn_;

    // Total uptime
    uint32_t total_uptime_saved_ = 0;
    uint32_t total_uptime_load_ = 0;
    bool total_uptime_load_ok_ = false;

    bool stats_save_h_null_ = false;
    bool stats_save_c_null_ = false;
    bool stats_save_e_null_ = false;
    bool stats_save_cal_null_ = false;
    bool stats_load_h_null_ = false;
    bool stats_load_c_null_ = false;
    bool stats_load_e_null_ = false;
    bool stats_load_cal_null_ = false;
    uint32_t stats_save_bs_ = 0xFFFFFFFF;
    float stats_save_integ_ = -1.0f;

    // Meter / correction log tracking
    bool meter_save_called_ = false;
    bool meter_load_called_ = false;
    bool meter_load_returns_ = false;
    NvsMeterBlob saved_meter_blob_;

private:
    bool predict_returns_ = false;
    float predict_rates_[3] = {};
    int predict_idx_ = 0;
    int predict_count_ = 0;
};
