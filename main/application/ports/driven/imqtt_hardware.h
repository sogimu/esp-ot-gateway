#pragma once

#include <cstddef>

/// Абстракция MQTT-клиента.
/// Реализация: MqttClientAdapter (ESP-IDF), FakeMqttHardware (тесты).
class IMqttHardware {
public:
    /// Уровень качества обслуживания MQTT
    enum class QoS {
        AT_MOST_ONCE  = 0,  ///< Доставка не более одного раза (без подтверждения)
        AT_LEAST_ONCE = 1,  ///< Доставка не менее одного раза (с подтверждением)
        EXACTLY_ONCE  = 2   ///< Доставка ровно один раз (четырёхэтапное рукопожатие)
    };

    /// Тип колбека для событий MQTT-клиента.
    /// Вызывается из контекста задачи MQTT-клиента (не из poll-задачи).
    /// @param event_id  Код события (константы MQTT_EVENT_*)
    /// @param event_data Указатель на структуру события (зависит от event_id)
    /// @param user_ctx   Пользовательский контекст, переданный в set_event_callback
    using EventCallback = void (*)(int event_id, void* event_data, void* user_ctx);

    virtual ~IMqttHardware() = default;

    /// Подключиться к MQTT-брокеру.
    /// @param uri             Полный URI брокера (mqtt://host:port или mqtts://host:port)
    /// @param user            Имя пользователя (может быть nullptr)
    /// @param pass            Пароль (может быть nullptr)
    /// @param lwt_topic       Топик Last Will Testament
    /// @param lwt_msg         Сообщение LWT ("offline")
    /// @param clean_session   Начинать чистую сессию
    /// @param keepalive_sec   Интервал keepalive в секундах
    /// @return true если подключение инициировано успешно
    virtual bool connect(const char* uri, const char* user, const char* pass,
                         const char* lwt_topic, const char* lwt_msg,
                         bool clean_session, int keepalive_sec) = 0;

    /// Отключиться от брокера
    virtual void disconnect() = 0;

    /// Переподключиться без пересоздания клиента (сохраняет handle).
    /// @return true если переподключение инициировано
    virtual bool reconnect() = 0;

    /// @return true если подключение активно
    virtual bool is_connected() const = 0;

    /// Опубликовать сообщение.
    /// @param topic   Топик назначения
    /// @param data    Данные сообщения
    /// @param len     Длина данных (-1 для строк с нулевым окончанием)
    /// @param qos     Уровень QoS
    /// @param retain  Флаг retained-сообщения
    /// @return ID сообщения (>0 при успехе) или -1 при ошибке
    virtual int publish(const char* topic, const char* data, int len,
                        QoS qos, bool retain) = 0;

    /// Подписаться на топик
    /// @param topic  Топик для подписки
    /// @param qos    Уровень QoS
    /// @return ID сообщения подписки (>0 при успехе) или -1 при ошибке
    virtual int subscribe(const char* topic, QoS qos) = 0;

    /// Отписаться от топика
    /// @param topic  Топик для отписки
    /// @return ID сообщения отписки (>0 при успехе) или -1 при ошибке
    virtual int unsubscribe(const char* topic) = 0;

    /// Установить колбек для событий MQTT-клиента.
    /// @param cb       Функция обратного вызова
    /// @param user_ctx Пользовательский контекст (будет передан в cb)
    virtual void set_event_callback(EventCallback cb, void* user_ctx) = 0;
};
