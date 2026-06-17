#pragma once

#include <cstddef>
#include <cstdint>

/// Интерфейс сохранения/загрузки настроек MQTT.
/// Выделен из NvsConfigAdapter для возможности тестирования MqttInteractor на хосте.
class IMqttConfigPersistence {
public:
    virtual ~IMqttConfigPersistence() = default;

    /// Сохранить настройки MQTT в энергонезависимую память.
    virtual void save_mqtt_config(const char* host, uint16_t port,
                                  const char* user, const char* pass,
                                  const char* prefix, bool enabled, bool tls) = 0;

    /// Загрузить настройки MQTT из энергонезависимой памяти.
    /// При отсутствии ключа в памяти значение параметра не меняется.
    /// @return true если NVS доступен (хотя бы частично)
    virtual bool load_mqtt_config(char* host, size_t host_size,
                                  uint16_t& port,
                                  char* user, size_t user_size,
                                  char* pass, size_t pass_size,
                                  char* prefix, size_t prefix_size,
                                  bool& enabled, bool& tls) = 0;
};
