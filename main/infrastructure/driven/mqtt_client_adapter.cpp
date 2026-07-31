#include "infrastructure/driven/mqtt_client_adapter.h"
#include "application/ports/driven/imqtt_message_sink.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static const char* TAG = "mqtt_cli";

// ── Конструктор / деструктор ────────────────────────────────

MqttClientAdapter::MqttClientAdapter(IMqttMessageSink& sink)
    : sink_(sink)
{
    sub_count_ = 0;
}

MqttClientAdapter::~MqttClientAdapter()
{
    disconnect();
}

// ── IMqttHardware ────────────────────────────────────────────

bool MqttClientAdapter::connect(const char* uri, const char* user, const char* pass,
                                 const char* lwt_topic, const char* lwt_msg,
                                 bool /*clean_session*/, int keepalive_sec)
{
    // Save parameters for reconnect()
    snprintf(saved_uri_, sizeof(saved_uri_), "%s", uri);
    snprintf(saved_user_, sizeof(saved_user_), "%s", user ? user : "");
    snprintf(saved_pass_, sizeof(saved_pass_), "%s", pass ? pass : "");
    snprintf(saved_lwt_topic_, sizeof(saved_lwt_topic_), "%s", lwt_topic ? lwt_topic : "");
    snprintf(saved_lwt_msg_, sizeof(saved_lwt_msg_), "%s", lwt_msg ? lwt_msg : "");
    saved_keepalive_ = keepalive_sec;
    has_saved_params_ = true;

    // If already connected, nothing to do
    if (client_ && connected_) return true;

    // If handle exists but disconnected, just reconnect — no alloc/free
    if (client_) {
        ESP_LOGI(TAG, "MQTT: переподключение (reuse handle)");
        esp_err_t err = esp_mqtt_client_reconnect(static_cast<esp_mqtt_client_handle_t>(client_));
        if (err == ESP_OK) return true;
        ESP_LOGW(TAG, "MQTT: переподключение не удалось, пересоздаём клиента");
        // Fall through — destroy and recreate
        esp_mqtt_client_destroy(static_cast<esp_mqtt_client_handle_t>(client_));
        client_ = nullptr;
        connected_ = false;
    }

    // First-time init
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri;
    cfg.credentials.username = user;
    cfg.credentials.authentication.password = pass;
    cfg.session.last_will.topic = lwt_topic;
    cfg.session.last_will.msg = lwt_msg;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = keepalive_sec;
    cfg.session.disable_clean_session = 0;
    // Let ESP-IDF auto-reconnect state machine handle reconnection —
    // cleanly frees old resources before each retry. Manual reconnect
    // via esp_mqtt_client_reconnect() causes resource overlap and heap leak.
    cfg.network.disable_auto_reconnect = false;
    cfg.task.stack_size = 4096;
    cfg.task.priority   = 7;  // выше control_loop(4), ниже lwip(18)/WiFi(23)
    cfg.session.message_retransmit_timeout = 2000;  // retransmit interval
    cfg.outbox.limit = 4096;                        // room for status(~1KB)+stats(~2KB)

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "Ошибка инициализации MQTT-клиента");
        return false;
    }

    esp_mqtt_client_register_event(static_cast<esp_mqtt_client_handle_t>(client_),
                                    MQTT_EVENT_ANY,
                                    esp_event_handler,
                                    this);

    esp_err_t err = esp_mqtt_client_start(static_cast<esp_mqtt_client_handle_t>(client_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска MQTT-клиента: %d", err);
        esp_mqtt_client_destroy(static_cast<esp_mqtt_client_handle_t>(client_));
        client_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "MQTT-клиент запущен: %s", uri);
    return true;
}

void MqttClientAdapter::disconnect()
{
    if (client_) {
        esp_mqtt_client_stop(static_cast<esp_mqtt_client_handle_t>(client_));
        // Don't destroy — keep handle for reconnect(). Only destroy if
        // settings change (caller must expliticly destroy + re-init).
        // This prevents the outbox leak described in ESP-IDF docs.
    }
    connected_ = false;
    sub_count_ = 0;
    ESP_LOGI(TAG, "MQTT-клиент остановлен");
}

bool MqttClientAdapter::is_connected() const
{
    return connected_;
}

bool MqttClientAdapter::reconnect()
{
    if (!client_ || !has_saved_params_) return false;

    esp_err_t err = esp_mqtt_client_reconnect(static_cast<esp_mqtt_client_handle_t>(client_));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ошибка реконнекта MQTT: %d", err);
        return false;
    }
    return true;
}

int MqttClientAdapter::publish(const char* topic, const char* data, int len,
                                QoS qos, bool retain)
{
    if (!client_ || !connected_) return -1;

    int msg_id = esp_mqtt_client_publish(
        static_cast<esp_mqtt_client_handle_t>(client_),
        topic, data, len, static_cast<int>(qos), retain ? 1 : 0);

    return msg_id;
}

int MqttClientAdapter::subscribe(const char* topic, QoS qos)
{
    if (!client_) return -1;

    // Store for re-subscription on reconnect
    if (sub_count_ < MAX_SUBS) {
        bool duplicate = false;
        for (int i = 0; i < sub_count_; i++) {
            if (strcmp(subs_[i].topic, topic) == 0) { duplicate = true; break; }
        }
        if (!duplicate) {
            snprintf(subs_[sub_count_].topic, sizeof(subs_[sub_count_].topic), "%s", topic);
            subs_[sub_count_].qos = qos;
            sub_count_++;
        }
    }

    if (!connected_) return 0;

    // QoS 0 for subscribe — SUBACK not needed, doesn't consume outbox
    return esp_mqtt_client_subscribe(
        static_cast<esp_mqtt_client_handle_t>(client_), topic, 0);
}

int MqttClientAdapter::unsubscribe(const char* topic)
{
    if (!client_) return -1;
    return esp_mqtt_client_unsubscribe(
        static_cast<esp_mqtt_client_handle_t>(client_), topic);
}

void MqttClientAdapter::set_event_callback(EventCallback cb, void* user_ctx)
{
    user_cb_ = cb;
    user_ctx_ = user_ctx;
}

// ── Обработчик событий ESP-MQTT ─────────────────────────────

void MqttClientAdapter::esp_event_handler(void* handler_args,
                                           const char* /*event_base*/,
                                           int32_t event_id,
                                           void* event_data)
{
    auto* self = static_cast<MqttClientAdapter*>(handler_args);
    self->on_event(static_cast<int>(event_id), event_data);
}

void MqttClientAdapter::on_event(int event_id, void* event_data)
{
    switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Подключён к брокеру");
        connected_ = true;

        // Переподписаться на все топики (QoS 0 — не забивает outbox)
        for (int i = 0; i < sub_count_; i++) {
            esp_mqtt_client_subscribe(
                static_cast<esp_mqtt_client_handle_t>(client_),
                subs_[i].topic, 0);
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        ESP_LOGI(TAG, "Отключён от брокера");
        connected_ = false;
        break;
    }
    case MQTT_EVENT_DATA: {
        auto* d = static_cast<esp_mqtt_event_handle_t>(event_data);
        if (d && d->topic && d->data) {
            IMqttMessageSink::Message msg;
            snprintf(msg.topic, sizeof(msg.topic),
                     "%.*s", std::min(d->topic_len, (int)sizeof(msg.topic) - 1),
                     d->topic);
            int copy_len = std::min(d->data_len, (int)sizeof(msg.payload) - 1);
            memcpy(msg.payload, d->data, (size_t)copy_len);
            msg.payload[copy_len] = '\0';
            msg.payload_len = copy_len;

            if (!sink_.push(msg)) {
                ESP_LOGW(TAG, "Очередь команд MQTT переполнена — сообщение отброшено");
            }
        }
        break;
    }
    case MQTT_EVENT_ERROR: {
        ESP_LOGE(TAG, "Ошибка MQTT");
        break;
    }
    default:
        break;
    }

    // Пробросить событие пользовательскому колбеку
    if (user_cb_) {
        user_cb_(event_id, event_data, user_ctx_);
    }
}
