#pragma once

#include "application/ports/driven/iboiler_config_store.h"

/// NVS-backed хранилище конфигурации котла (CH/DHW/PID/калибровка/КПД).
/// Использует namespace "config" (тот же, что и NvsConfigStore и MqttNvsStore).
/// Выделен из NvsConfigStore — SystemConfigInteractor получает ссылку на этот
/// интерфейс (как на IMqttConfigStore для MQTT).
class BoilerNvsStore : public IBoilerConfigStore {
public:
    void load_boiler_config(IHeatingStateStore& state) override;
    void save_boiler_config(const IHeatingStateStore& state) override;
};
