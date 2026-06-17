#pragma once

#include <cstddef>
#include <cstdint>

/// Конфигуратор MQTT — driving-порт для HTTP API.
/// Реализуется MqttInteractor'ом, используется HttpControllerAdapter'ом.
/// HTTP-слой не знает о MQtt-деталях (соблюдается правило: web не знает про mqtt).
class IMqttConfigurator {
public:
    virtual ~IMqttConfigurator() = default;

    // ── Геттеры текущих настроек ─────────────────────────

    virtual bool is_enabled() const = 0;
    virtual bool is_connected() const = 0;
    virtual const char* get_host() const = 0;
    virtual uint16_t get_port() const = 0;
    virtual const char* get_user() const = 0;
    virtual const char* get_prefix() const = 0;
    virtual bool get_tls() const = 0;

    // ── Применить настройки ──────────────────────────────

    /// Сохранить настройки MQTT в NVS и переподключиться с новыми параметрами.
    /// @param host     Адрес брокера
    /// @param port     Порт (обычно 1883 или 8883)
    /// @param user     Логин (может быть "")
    /// @param pass     Пароль (может быть "")
    /// @param prefix   Префикс топиков
    /// @param enabled  Включить MQTT
    /// @param tls      Использовать TLS
    virtual void save_and_apply(const char* host, uint16_t port,
                                const char* user, const char* pass,
                                const char* prefix, bool enabled, bool tls) = 0;
};
