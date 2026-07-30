#pragma once
#include "application/ports/driven/iburn_stats_store.h"

/// NVS-backed хранилище статистики горелки. Namespace "stats", скалярные ключи.
/// Выделен из NvsConfigStore.
class BurnStatsNvsStore : public IBurnStatsStore {
public:
    void save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
                         uint32_t inter_pause_sec, uint32_t inter_cnt,
                         uint32_t mod_pause_sec, uint32_t mod_cnt) override;
    bool load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
                         uint32_t& inter_pause_sec, uint32_t& inter_cnt,
                         uint32_t& mod_pause_sec, uint32_t& mod_cnt) override;
};
