#pragma once

#include "application/ports/driven/imqtt_hardware.h"
#include <cstdint>

class IMqttMessageSink;

/// Адаптер ESP-MQTT клиента. Реализует IMqttHardware.
///
/// Управляет жизненным циклом подключения к MQTT-брокеру:
///   - Last Will Testament (LWT) для обнаружения недоступности
///   - Автоматическая переподписка после реконнекта
///   - Exponential backoff при обрывах соединения
///
/// НЕ обрабатывает содержимое входящих сообщений — только передаёт их
/// в IMqttMessageSink через неблокирующий push().
///
/// Заголовок НЕ включает <mqtt_client.h> — используется void* для
/// изоляции зависимости от ESP-IDF.
class MqttClientAdapter : public IMqttHardware {
public:
    /// @param sink  Приёмник входящих MQTT-сообщений (неблокирующий push)
    explicit MqttClientAdapter(IMqttMessageSink& sink);
    ~MqttClientAdapter() override;

    // ── IMqttHardware ──────────────────────────────────────

    bool connect(const char* uri, const char* user, const char* pass,
                 const char* lwt_topic, const char* lwt_msg,
                 bool clean_session, int keepalive_sec) override;
    void disconnect() override;
    bool reconnect() override;
    bool is_connected() const override;
    int  publish(const char* topic, const char* data, int len,
                 QoS qos, bool retain) override;
    int  subscribe(const char* topic, QoS qos) override;
    int  unsubscribe(const char* topic) override;
    void set_event_callback(EventCallback cb, void* user_ctx) override;

    // ── Управление реконнектом (вызывается из MqttInteractor::poll) ──

    /// Проверить, пора ли пытаться переподключиться (exponential backoff).
    /// @param now_us  Текущее монотонное время в микросекундах
    bool should_reconnect(uint64_t now_us) const;

    /// Сбросить задержку реконнекта в 1с (после успешного подключения).
    void reset_backoff();

    /// Запланировать следующую попытку реконнекта (после разрыва).
    /// @param now_us  Текущее монотонное время в микросекундах
    void schedule_reconnect(uint64_t now_us);

private:
    /// Статический обработчик событий ESP-MQTT (esp_event_handler_t).
    /// ESP-IDF v5.2 сигнатура: (void* arg, const char* base, int32_t id, void* data)
    static void esp_event_handler(void* handler_args,
                                   const char* event_base,
                                   int32_t event_id,
                                   void* event_data);

    /// Обработчик событий экземпляра.
    void on_event(int event_id, void* event_data);

    // ── Поля ─────────────────────────────────────────────────

    void* client_ = nullptr;         ///< esp_mqtt_client_handle_t (void* для изоляции)
    IMqttMessageSink& sink_;         ///< Приёмник входящих сообщений
    EventCallback  user_cb_ = nullptr;
    void*          user_ctx_ = nullptr;
    bool           connected_ = false;

    // Подписки для восстановления после реконнекта
    static constexpr int MAX_SUBS = 8;
    struct SubEntry {
        char topic[128];
        QoS  qos = QoS::AT_MOST_ONCE;
    };
    SubEntry subs_[MAX_SUBS];
    int      sub_count_ = 0;

    // Exponential backoff
    int      reconnect_delay_s_   = 1;
    uint64_t reconnect_after_us_  = 0;
};
