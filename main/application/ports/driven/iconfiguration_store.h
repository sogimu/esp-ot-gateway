#pragma once

#include <stdint.h>

/// Non-volatile configuration persistence.
/// Loads at boot, saves on config change and periodic tick.
class IConfigurationStore {
public:
    /// Load all 4 NVS namespaces (config, stats, meter, predict).
    virtual void load_all(class IHeatingStateStore& state) = 0;

    /// Save user config namespace only (time settings — tz_offset, SNTP).
    virtual void save_config(const class IHeatingStateStore& state) = 0;

    virtual ~IConfigurationStore() = default;
};
