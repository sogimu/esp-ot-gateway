#pragma once

#include "application/ports/driven/imqtt_config_persistence.h"
#include <cstring>
#include <cstdio>

/// Fake-реализация IMqttConfigPersistence для хостовых тестов.
/// Хранит настройки MQTT в памяти.
class FakeMqttConfigPersistence : public IMqttConfigPersistence {
public:
    void save_mqtt_config(const char* host, uint16_t port,
                          const char* user, const char* pass,
                          const char* prefix, bool enabled, bool tls) override
    {
        snprintf(host_, sizeof(host_), "%s", host ? host : "");
        port_ = port;
        snprintf(user_, sizeof(user_), "%s", user ? user : "");
        snprintf(pass_, sizeof(pass_), "%s", pass ? pass : "");
        snprintf(prefix_, sizeof(prefix_), "%s", prefix ? prefix : "");
        enabled_ = enabled;
        tls_ = tls;
        save_called_ = true;
    }

    bool load_mqtt_config(char* host, size_t host_size, uint16_t& port,
                          char* user, size_t user_size,
                          char* pass, size_t pass_size,
                          char* prefix, size_t prefix_size,
                          bool& enabled, bool& tls) override
    {
        if (!save_called_) return false;

        snprintf(host, host_size, "%s", host_);
        port = port_;
        snprintf(user, user_size, "%s", user_);
        snprintf(pass, pass_size, "%s", pass_);
        snprintf(prefix, prefix_size, "%s", prefix_);
        enabled = enabled_;
        tls = tls_;
        load_called_ = true;
        return true;
    }

    /// Настроить возвращаемые значения без вызова save
    void preset(const char* host, uint16_t port,
                const char* user, const char* pass,
                const char* prefix, bool enabled, bool tls)
    {
        snprintf(host_, sizeof(host_), "%s", host);
        port_ = port;
        snprintf(user_, sizeof(user_), "%s", user);
        snprintf(pass_, sizeof(pass_), "%s", pass);
        snprintf(prefix_, sizeof(prefix_), "%s", prefix);
        enabled_ = enabled;
        tls_ = tls;
        save_called_ = true;  // чтобы load() вернул true
    }

    bool save_called_ = false;
    bool load_called_ = false;
    char host_[128] = {};
    uint16_t port_ = 1883;
    char user_[64] = {};
    char pass_[64] = {};
    char prefix_[64] = {};
    bool enabled_ = false;
    bool tls_ = false;
};
