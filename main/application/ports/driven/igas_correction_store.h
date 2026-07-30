#pragma once

class IHeatingStateStore;

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
};
