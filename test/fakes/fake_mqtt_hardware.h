#pragma once

#include "application/ports/driven/imqtt_hardware.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

/// Fake-реализация IMqttHardware для хостовых тестов.
/// Позволяет симулировать события MQTT и проверять опубликованные сообщения.
class FakeMqttHardware : public IMqttHardware {
public:
    /// Запись об опубликованном сообщении
    struct PublishedMsg {
        char topic[128];
        char data[1024];
        QoS  qos = QoS::AT_MOST_ONCE;
        bool retain = false;
    };

    // ── IMqttHardware ──────────────────────────────────────

    bool connect(const char*, const char*, const char*,
                 const char*, const char*, bool, int) override {
        connected_ = true;
        return true;
    }

    void disconnect() override { connected_ = false; }
    bool reconnect() override { return true; }  // тестовый reconnect всегда успешен
    bool is_connected() const override { return connected_; }

    int publish(const char* topic, const char* data, int len,
                QoS qos, bool retain) override {
        PublishedMsg m;
        snprintf(m.topic, sizeof(m.topic), "%s", topic);
        int copy_len = (len >= 0)
            ? std::min(len, (int)sizeof(m.data) - 1)
            : (int)strlen(data);
        memcpy(m.data, data, (size_t)copy_len);
        m.data[copy_len] = '\0';
        m.qos = qos;
        m.retain = retain;
        publishes_.push_back(m);
        return (int)publishes_.size();
    }

    int subscribe(const char*, QoS) override { return 1; }
    int unsubscribe(const char*) override { return 1; }

    void set_event_callback(EventCallback cb, void* ctx) override {
        cb_ = cb;
        ctx_ = ctx;
    }

    // ── Инжекция событий (только для тестов) ─────────────

    /// Сымитировать успешное подключение
    void inject_connected() {
        connected_ = true;
        if (cb_) cb_(0 /* MQTT_EVENT_CONNECTED */, nullptr, ctx_);
    }

    /// Сымитировать разрыв соединения
    void inject_disconnected() {
        connected_ = false;
        if (cb_) cb_(1 /* MQTT_EVENT_DISCONNECTED */, nullptr, ctx_);
    }

    /// Сымитировать входящее MQTT-сообщение
    void inject_message(const char* topic, const char* data, int len) {
        if (!cb_) return;
        struct FakeEventData {
            char topic[128];
            int  topic_len;
            char data[1024];
            int  data_len;
        };
        FakeEventData d;
        snprintf(d.topic, sizeof(d.topic), "%s", topic);
        d.topic_len = (int)strlen(d.topic);
        int copy_len = std::min(len, (int)sizeof(d.data) - 1);
        memcpy(d.data, data, (size_t)copy_len);
        d.data[copy_len] = '\0';
        d.data_len = copy_len;
        cb_(2 /* MQTT_EVENT_DATA */, &d, ctx_);
    }

    // ── Поиск опубликованных сообщений ───────────────────

    /// Найти последнюю публикацию в заданный топик (по подстроке)
    const PublishedMsg* last_publish_to(const char* topic_substr) const {
        for (auto it = publishes_.rbegin(); it != publishes_.rend(); ++it) {
            if (strstr(it->topic, topic_substr)) return &*it;
        }
        return nullptr;
    }

    /// Подсчитать количество публикаций в заданный топик
    int count_publishes_to(const char* topic_substr) const {
        int n = 0;
        for (const auto& p : publishes_) {
            if (strstr(p.topic, topic_substr)) n++;
        }
        return n;
    }

    // ── Публичные поля ────────────────────────────────────

    bool connected_ = false;
    EventCallback cb_ = nullptr;
    void* ctx_ = nullptr;
    std::vector<PublishedMsg> publishes_;
};
