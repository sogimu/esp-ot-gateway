#pragma once
#include "application/ports/driven/iheating_stats_store.h"

/// NVS-backed хранилище статистики нагрева и счётчика.
/// Namespaces: "stats" (hist/cycles/gas_ema/calib/integ_m3/uptime), "meter".
/// Выделен из NvsConfigStore.
class HeatingStatsNvsStore : public IHeatingStatsStore {
public:
    void save_stats(const IHeatingStateStore&, uint32_t burner_sec, float integ_m3,
                    const void* hist, const void* cycles,
                    const void* ema, const void* calib) override;
    bool load_stats(uint32_t& burner_sec, float& integ_m3,
                    void* hist, void* cycles, void* ema, void* calib) override;

    void save_total_uptime(uint32_t total_uptime_sec) override;
    bool load_total_uptime(uint32_t& total_uptime_sec) override;

    void save_integral(float value) override;

    void save_meter(const IHeatingStateStore&, const void* blob = nullptr) override;
    bool load_meter(IHeatingStateStore&, void* blob = nullptr) override;
};
