#pragma once

#include "application/ports/driven/iconfiguration_store.h"
#include <cstdint>

// ── NVS blob structures (on-disk format) ──────────────────────

#define HIST_BINS 1000
#define CYCLE_RING 256
#define CORRECTION_LOG_SIZE 32

struct __attribute__((packed)) NvsHistBlob {
    uint32_t samples;
    uint16_t hist[HIST_BINS];  // NVS storage: 16-bit (blob fits in 2KB limit)
};
static_assert(sizeof(NvsHistBlob) == 4 + 1000 * 2, "NvsHistBlob size mismatch");

struct __attribute__((packed)) NvsCycleBlob {
    uint32_t burner_sec; uint32_t cycle_cnt;
    int32_t cycle_idx; int32_t cycle_total;
    uint16_t burn_dur[CYCLE_RING]; uint16_t pause_dur[CYCLE_RING];
};
static_assert(sizeof(NvsCycleBlob) == 4 + 4 + 4 + 4 + 256 * 2 + 256 * 2, "NvsCycleBlob size mismatch");

struct __attribute__((packed)) NvsGasEmaBlob {
    float ema_1h, ema_3h, ema_12h, ema_24h, ema_7d;
    uint64_t ema_start_us;
};
static_assert(sizeof(NvsGasEmaBlob) == 5 * 4 + 8, "NvsGasEmaBlob size mismatch");

// ── Boiler model config blob (on-disk format) ──────────────────
struct __attribute__((packed)) NvsCalibBlob {
    float k_calib, p_max, gas_calorific;     // existing (offset 0)
    float gas_temp_offset;                   // default -5.0
    float ch_pmin_warm, ch_pmax_warm;        // default 3.7, 21.8
    float ch_pmin_hot,  ch_pmax_hot;         // default 3.4, 20.0
    float dhw_pmin,     dhw_pmax;            // default 5.0, 24.0
};
static_assert(sizeof(NvsCalibBlob) == 40, "NvsCalibBlob size mismatch");

struct __attribute__((packed)) NvsEfficiencyBlob {
    float t1, v1;  // 30, 0.98
    float t2, v2;  // 55, 0.93
    float t3, v3;  // 80, 0.88
};
static_assert(sizeof(NvsEfficiencyBlob) == 24, "NvsEfficiencyBlob size mismatch");

struct __attribute__((packed)) NvsCorrLogEntry {
    uint32_t timestamp; float actual_reading, estimated_total, difference, prev_k_calib, new_k_calib;
};
static_assert(sizeof(NvsCorrLogEntry) == 4 + 4 + 4 + 4 + 4 + 4, "NvsCorrLogEntry size mismatch");

struct __attribute__((packed)) NvsMeterBlob {
    float base_reading, last_correction_actual, integral_at_last_correction;
    int32_t corrections_head, corrections_count;
    NvsCorrLogEntry corrections[CORRECTION_LOG_SIZE];
};
static_assert(sizeof(NvsMeterBlob) == 4 + 4 + 4 + 4 + 4 + 32 * (4 + 4 + 4 + 4 + 4 + 4), "NvsMeterBlob size mismatch");

struct __attribute__((packed)) NvsPredictBlob { float rates[3]; int32_t idx, count; };
static_assert(sizeof(NvsPredictBlob) == 3 * 4 + 4 + 4, "NvsPredictBlob size mismatch");

// ── CA NVS adapter (standalone, no MVC dependencies) ──────────

class NvsConfigAdapter : public IConfigurationStore {
public:
    void init();   // nvs_flash_init

    void load_all(IHeatingStateStore& state) override;
    void save_config(const IHeatingStateStore& state) override;
    void save_stats(const IHeatingStateStore&, uint32_t, float,
                     const void*, const void*, const void*, const void*) override;
    bool load_stats(uint32_t&, float&, void*, void*, void*, void*) override;
    void save_meter(const IHeatingStateStore&, const void* blob = nullptr) override;
    bool load_meter(IHeatingStateStore&, void* blob = nullptr) override;
    void save_predict(const float[3], int, int) override;
    bool load_predict(float[3], int&, int&) override;

    void save_total_uptime(uint32_t total_uptime_sec) override;
    bool load_total_uptime(uint32_t& total_uptime_sec) override;

    void save_integral(float value) override;

    bool save_eff(const IHeatingStateStore& state);
    bool load_eff(IHeatingStateStore& state);

    void save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
                         uint32_t inter_pause_sec, uint32_t inter_cnt,
                         uint32_t mod_pause_sec, uint32_t mod_cnt) override;
    bool load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
                         uint32_t& inter_pause_sec, uint32_t& inter_cnt,
                         uint32_t& mod_pause_sec, uint32_t& mod_cnt) override;
};
