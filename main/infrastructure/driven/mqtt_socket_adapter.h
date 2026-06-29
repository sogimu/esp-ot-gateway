#pragma once

#include "application/ports/driven/imqtt_hardware.h"
#include "application/ports/driven/imqtt_message_sink.h"
#include "application/ports/driven/itime_source.h"
#include <cstdint>

/// Минимальный MQTT 3.1.1 клиент поверх TCP сокета.
/// Ноль malloc — вся кодировка в static-буферы на стеке.
/// Заменяет esp_mqtt_client для publish, сохраняя совместимость с IMqttHardware.
class MqttSocketAdapter : public IMqttHardware {
public:
    MqttSocketAdapter(IMqttMessageSink& sink, ITimeSource& time);
    ~MqttSocketAdapter() override;

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

    /// Чтение входящих, keepalive, таймауты — вызывать периодически.
    void poll_socket() override;

private:
    // ── Socket helpers ─────────────────────────────────────
    bool try_connect();
    bool send_connect_packet();
    bool send_publish_packet(const char* topic, const char* data,
                             int len, bool retain);
    bool send_subscribe_packet();
    bool send_pingreq();
    int  read_byte(int timeout_ms);
    int  read_exact(uint8_t* buf, int len, int timeout_ms);
    void process_incoming();

    // ── MQTT encoding (все в static буферы) ────────────────
    static int encode_remaining_length(uint8_t* buf, int len);
    static int write_u16(uint8_t* buf, uint16_t v);
    static int write_string(uint8_t* buf, const char* s, int len);

    // ── Состояние ──────────────────────────────────────────
    int  sock_ = -1;
    bool connected_ = false;
    bool subscribed_ = false;
    bool connect_pending_ = false;

    IMqttMessageSink& sink_;
    ITimeSource& time_;
    EventCallback user_cb_ = nullptr;
    void*        user_ctx_ = nullptr;

    // Сохранённые параметры
    char uri_[128] = {};
    int  keepalive_s_ = 60;
    char lwt_topic_[128] = {};
    char lwt_msg_[32] = {};

    // Топики подписки
    static constexpr int MAX_SUBS = 4;
    char sub_topics_[MAX_SUBS][128] = {};
    int  sub_count_ = 0;

    // Таймеры
    uint64_t last_ping_us_ = 0;
    uint64_t ping_sent_us_ = 0;
    bool     ping_pending_ = false;
    uint64_t last_connect_attempt_us_ = 0;
    int      connect_failures_ = 0;

    static constexpr int BUF_SIZE = 2048;
};
