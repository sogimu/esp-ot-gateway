#pragma once
#include <cstdint>

class IHeatingStateStore;

static constexpr int GAS_DAILY_SLOTS = 8;

struct GasDailyBlob {
    int64_t epoch_days[GAS_DAILY_SLOTS];
    float   m3_values[GAS_DAILY_SLOTS];
    int32_t head;
    int32_t count;
    int64_t today_epoch_day;
    float   today_m3;
};

/// Driven-порт: персистентность данных газовой коррекции (журнал сверки счётчика)
/// и калибровки котла, которую меняет GasCorrectionInteractor.
///
/// Выделен из NvsConfigStore — аналогично BoilerNvsStore. Сохранение котловой
/// конфигурации (k_calib, gas_calorific, …) делегируется IBoilerConfigStore
/// (без дублирования логики).
class IGasCorrectionStore {
public:
    virtual ~IGasCorrectionStore() = default;

    /// Загрузить журнал сверки (NvsMeterBlob) из NVS. true = данные есть.
    virtual bool load_meter(IHeatingStateStore& state, void* blob = nullptr) = 0;

    /// Сохранить журнал сверки в NVS.
    virtual void save_meter(const IHeatingStateStore& state, const void* blob = nullptr) = 0;

    /// Сохранить integral_m3 в NVS (stats namespace).
    virtual void save_integral(float value) = 0;

    /// Сохранить котловую конфигурацию (калибровку) — делегирует IBoilerConfigStore.
    virtual void save_boiler_config(const IHeatingStateStore& state) = 0;

    virtual void save_daily_gas(const void* blob)  { (void)blob; }
    virtual bool load_daily_gas(void* blob)        { (void)blob; return false; }
};
