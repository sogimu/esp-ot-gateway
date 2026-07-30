#pragma once
#include <cstdint>

/// Driven-порт: персистентность статистики горелки (burner seconds, паузы, циклы).
/// Выделен из IConfigurationStore/NvsConfigStore. Используется main.cpp (восстановление
/// при старте, периодический save) и OTA flush-коллбеком.
class IBurnStatsStore {
public:
    virtual ~IBurnStatsStore() = default;

    virtual void save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
                                 uint32_t inter_pause_sec, uint32_t inter_cnt,
                                 uint32_t mod_pause_sec, uint32_t mod_cnt) = 0;
    virtual bool load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
                                 uint32_t& inter_pause_sec, uint32_t& inter_cnt,
                                 uint32_t& mod_pause_sec, uint32_t& mod_cnt) = 0;
};
