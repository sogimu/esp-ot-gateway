#pragma once

#include <stdint.h>

/// Non-volatile configuration persistence.
/// Loads at boot, saves on config change and periodic tick.
class IConfigurationStore {
public:
    /// Load all 4 NVS namespaces (config, stats, meter, predict).
    virtual void load_all(class IHeatingStateStore& state) = 0;

    /// Save user config namespace only (setpoints, enables, schedule, PID, SNTP).
    virtual void save_config(const class IHeatingStateStore& state) = 0;

    /// Save stats namespace (histogram, cycles, gas EMA, calibration).
    virtual void save_stats(const class IHeatingStateStore& state,
                             uint32_t burner_sec, float integ_m3,
                             const void* hist_blob, const void* cycles_blob,
                             const void* ema_blob, const void* calib_blob) = 0;

    /// Load stats namespace.
    virtual bool load_stats(uint32_t& burner_sec, float& integ_m3,
                             void* hist_blob, void* cycles_blob,
                             void* ema_blob, void* calib_blob) = 0;

    /// Save/load meter correction log.
    virtual void save_meter(const class IHeatingStateStore& state) = 0;
    virtual bool load_meter(class IHeatingStateStore& state) = 0;

    /// Save/load DHW prediction history.
    virtual void save_predict(const float rates[3], int idx, int count) = 0;
    virtual bool load_predict(float rates[3], int& idx, int& count) = 0;

    virtual ~IConfigurationStore() = default;
};
