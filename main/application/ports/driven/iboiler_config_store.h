#pragma once

class IHeatingStateStore;

/// Driven-порт: сохранение/загрузка конфигурации котла (CH/DHW/PID/калибровка).
/// Выделен из NvsConfigStore для уменьшения связности — SystemConfigInteractor
/// обращается к котловому конфигу через этот интерфейс, аналогично тому как
/// MqttInteractor использует IMqttConfigStore.
class IBoilerConfigStore {
public:
    virtual ~IBoilerConfigStore() = default;

    /// Загрузить все котловые настройки из NVS в состояние.
    /// При отсутствии ключа в памяти значение в состоянии не меняется.
    virtual void load_boiler_config(IHeatingStateStore& state) = 0;

    /// Сохранить все котловые настройки из состояния в NVS.
    virtual void save_boiler_config(const IHeatingStateStore& state) = 0;
};
