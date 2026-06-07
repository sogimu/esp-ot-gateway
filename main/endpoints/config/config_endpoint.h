#pragma once

#include <cstdint>
#include <cstddef>

// Forward declarations
class Model;
class PidService;
class SntpEndpoint;
class OpenthermEndpoint;

// Metric constants shared with stats service
#define HIST_BINS 1000
#define CYCLE_RING 256
#define CORRECTION_LOG_SIZE 32

// === NVS blob structures (on-disk format) ===

struct __attribute__((packed)) NvsHistBlob {
    uint32_t samples;
    uint16_t hist[HIST_BINS];
};

struct __attribute__((packed)) NvsCycleBlob {
    uint32_t burner_sec;
    uint32_t cycle_cnt;
    int32_t cycle_idx;
    int32_t cycle_total;
    uint16_t burn_dur[CYCLE_RING];
    uint16_t pause_dur[CYCLE_RING];
};

struct __attribute__((packed)) NvsGasEmaBlob {
    float ema_1h;
    float ema_3h;
    float ema_12h;
    float ema_24h;
    float ema_7d;
    uint64_t ema_start_us;
};

struct __attribute__((packed)) NvsCalibBlob {
    float k_calib;
    float p_max;
    float gas_calorific;
};

struct __attribute__((packed)) NvsCorrLogEntry {
    uint32_t timestamp;
    float actual_reading;
    float estimated_total;
    float difference;
    float prev_k_calib;
    float new_k_calib;
};

struct __attribute__((packed)) NvsMeterBlob {
    float base_reading;
    float last_correction_actual;
    float integral_at_last_correction;
    int32_t corrections_head;
    int32_t corrections_count;
    NvsCorrLogEntry corrections[CORRECTION_LOG_SIZE];
};

struct __attribute__((packed)) NvsPredictBlob {
    float rates[3];
    int32_t idx;
    int32_t count;
};

// === ConfigEndpoint ===

class ConfigEndpoint {
public:
    ConfigEndpoint() = default;

    /// One-shot NVS flash init (call once at boot, before any other method)
    void init();

    // ── "config" namespace ──────────────────────────────────────

    /// Read persisted config and apply to Model, PidService, endpoints.
    /// Returns silently if NVS is empty (first boot).
    void load_config(Model& model, PidService& pid,
                     SntpEndpoint& sntp, OpenthermEndpoint& ot);

    /// Persist current config from Model and PidService.
    void save_config(const Model& model, const PidService& pid);

    // ── "stats" namespace ───────────────────────────────────────

    bool load_stats(uint32_t& burner_sec, float& integ_m3,
                    NvsGasEmaBlob& ema, NvsHistBlob& hist,
                    NvsCycleBlob& cycles, NvsCalibBlob& calib);

    void save_stats(uint32_t burner_sec, float integ_m3,
                    const NvsGasEmaBlob& ema, const NvsHistBlob& hist,
                    const NvsCycleBlob& cycles, const NvsCalibBlob& calib);

    // ── "meter" namespace ───────────────────────────────────────

    bool load_meter(Model& model);
    void save_meter(const Model& model);

    // ── "predict" namespace ─────────────────────────────────────

    bool load_predict(float rates[3], int& idx, int& count);
    void save_predict(const float rates[3], int idx, int count);
};
