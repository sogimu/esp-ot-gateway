#pragma once
#include <cstdint>

class IHeatingStateStore;

/// Driven-порт: персистентность статистики нагрева (гистограмма, циклы, газ EMA,
/// калибровка, integral_m3, uptime) и журнала сверки счётчика.
/// Выделен из IConfigurationStore/NvsConfigStore. Используется main.cpp
/// и OTA flush-коллбеком.
class IHeatingStatsStore {
public:
    virtual ~IHeatingStatsStore() = default;

    virtual void save_stats(const IHeatingStateStore&, uint32_t burner_sec, float integ_m3,
                            const void* hist, const void* cycles,
                            const void* ema, const void* calib) = 0;
    virtual bool load_stats(uint32_t& burner_sec, float& integ_m3,
                            void* hist, void* cycles, void* ema, void* calib) = 0;

    virtual void save_total_uptime(uint32_t total_uptime_sec) = 0;
    virtual bool load_total_uptime(uint32_t& total_uptime_sec) = 0;

    virtual void save_integral(float value) = 0;

    virtual void save_meter(const IHeatingStateStore&, const void* blob = nullptr) = 0;
    virtual bool load_meter(IHeatingStateStore&, void* blob = nullptr) = 0;
};
