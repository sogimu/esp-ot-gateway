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

    /// Save meter correction log. Pass optional blob (NvsMeterBlob*) for full log persistence.
    virtual void save_meter(const class IHeatingStateStore& state, const void* blob = nullptr) = 0;
    /// Load meter correction log. Pass optional blob (NvsMeterBlob*) to restore full log.
    virtual bool load_meter(class IHeatingStateStore& state, void* blob = nullptr) = 0;

    /// Save/load total controller uptime across reboots.
    virtual void save_total_uptime(uint32_t total_uptime_sec) = 0;
    virtual bool load_total_uptime(uint32_t& total_uptime_sec) = 0;

    /// Persist gas integral separately (called after correction resets it to 0).
    virtual void save_integral(float value) = 0;

    virtual ~IConfigurationStore() = default;
};
