#pragma once

#include "application/ports/driven/iconfiguration_store.h"
#include <cstdint>

// ── NVS blob structures (on-disk format) ──────────────────────

#define HIST_BINS 1000
#define CYCLE_RING 256
#define CORRECTION_LOG_SIZE 32

struct __attribute__((packed)) NvsHistBlob {
    uint32_t samples;
    uint16_t hist[HIST_BINS];
};
struct __attribute__((packed)) NvsCycleBlob {
    uint32_t burner_sec; uint32_t cycle_cnt;
    int32_t cycle_idx; int32_t cycle_total;
    uint16_t burn_dur[CYCLE_RING]; uint16_t pause_dur[CYCLE_RING];
};
struct __attribute__((packed)) NvsGasEmaBlob {
    float ema_1h, ema_3h, ema_12h, ema_24h, ema_7d;
    uint64_t ema_start_us;
};
struct __attribute__((packed)) NvsCalibBlob { float k_calib, p_max, gas_calorific; };
struct __attribute__((packed)) NvsCorrLogEntry {
    uint32_t timestamp; float actual_reading, estimated_total, difference, prev_k_calib, new_k_calib;
};
struct __attribute__((packed)) NvsMeterBlob {
    float base_reading, last_correction_actual, integral_at_last_correction;
    int32_t corrections_head, corrections_count;
    NvsCorrLogEntry corrections[CORRECTION_LOG_SIZE];
};
struct __attribute__((packed)) NvsPredictBlob { float rates[3]; int32_t idx, count; };

// ── CA NVS adapter (standalone, no MVC dependencies) ──────────

class NvsConfigAdapter : public IConfigurationStore {
public:
    void init();   // nvs_flash_init

    void load_all(IHeatingStateStore& state) override;
    void save_config(const IHeatingStateStore& state) override;
    void save_stats(const IHeatingStateStore&, uint32_t, float,
                     const void*, const void*, const void*, const void*) override;
    bool load_stats(uint32_t&, float&, void*, void*, void*, void*) override;
    void save_meter(const IHeatingStateStore&) override;
    bool load_meter(IHeatingStateStore&) override;
    void save_predict(const float[3], int, int) override;
    bool load_predict(float[3], int&, int&) override;
};
