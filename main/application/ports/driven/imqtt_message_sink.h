#pragma once

#include <cstddef>

/// Потокобезопасный приёмник входящих MQTT-сообщений.
/// Используется для передачи команд из колбека MQTT-клиента (контекст MQTT-задачи)
/// в цикл обработки MqttInteractor::poll() (контекст poll-задачи).
///
/// Реализация:   FreeRtosMqttQueue  (на устройстве, через xQueue)
///               FakeMqttMessageSink (в тестах, однопоточный кольцевой буфер)
class IMqttMessageSink {
public:
    /// Входящее MQTT-сообщение
    struct Message {
        static constexpr int TOPIC_MAX   = 64;
        static constexpr int PAYLOAD_MAX = 512;

        char topic[TOPIC_MAX];
        char payload[PAYLOAD_MAX];
        int  payload_len;   ///< Фактическая длина данных в payload (0..PAYLOAD_MAX-1)
    };

    virtual ~IMqttMessageSink() = default;

    /// Положить сообщение в очередь (неблокирующий вызов).
    /// Вызывается ТОЛЬКО из MQTT-колбека.
    /// @return true если сообщение помещено в очередь
    /// @return false если очередь переполнена (сообщение отброшено)
    virtual bool push(const Message& msg) = 0;

    /// Извлечь сообщение из очереди (неблокирующий вызов).
    /// Вызывается ТОЛЬКО из MqttInteractor::poll().
    /// @param[out] msg  Извлечённое сообщение (только при возврате true)
    /// @return true если сообщение извлечено
    /// @return false если очередь пуста
    virtual bool pop(Message& msg) = 0;
};
