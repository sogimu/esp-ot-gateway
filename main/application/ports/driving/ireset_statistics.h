#pragma once

/// Reset accumulated statistics.
class IResetStatistics {
public:
    virtual void reset_modulation_stats() = 0;
    virtual void reset_cycle_stats() = 0;
    virtual void reset_gas_stats() = 0;
    virtual ~IResetStatistics() = default;
};
