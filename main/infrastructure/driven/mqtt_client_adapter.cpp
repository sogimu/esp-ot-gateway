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
    if (client_) {
        disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));  // дать TCP-стеку время закрыть соединение
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri;
    cfg.credentials.username = user;
    cfg.credentials.authentication.password = pass;
    cfg.session.last_will.topic = lwt_topic;
    cfg.session.last_will.msg = lwt_msg;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = keepalive_sec;
    cfg.network.disable_auto_reconnect = true;   // без авто-реконнекта (бережём сокеты)
    cfg.task.stack_size = 8192;                   // больше дефолтных 6144

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "Ошибка инициализации MQTT-клиента");
        return false;
    }

    esp_mqtt_client_register_event((esp_mqtt_client_handle_t)client_,
                                    MQTT_EVENT_ANY,
                                    esp_event_handler,
                                    this);

    esp_err_t err = esp_mqtt_client_start((esp_mqtt_client_handle_t)client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска MQTT-клиента: %d", err);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t)client_);
        client_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "MQTT-клиент запущен: %s", uri);
    return true;
}

void MqttClientAdapter::disconnect()
{
    if (client_) {
        esp_mqtt_client_stop((esp_mqtt_client_handle_t)client_);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t)client_);
        client_ = nullptr;
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
    if (!client_) return false;
    esp_err_t err = esp_mqtt_client_reconnect((esp_mqtt_client_handle_t)client_);
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
        (esp_mqtt_client_handle_t)client_,
        topic, data, len, (int)qos, retain ? 1 : 0);

    return msg_id;
}

int MqttClientAdapter::subscribe(const char* topic, QoS qos)
{
    if (!client_) return -1;

    // Сохраняем топик всегда (даже если ещё не подключены — подпишемся при CONNECTED)
    if (sub_count_ < MAX_SUBS) {
        bool duplicate = false;
        for (int i = 0; i < sub_count_; i++) {
            if (strcmp(subs_[i].topic, topic) == 0) { duplicate = true; break; }
        }
        if (!duplicate) {
            snprintf(subs_[sub_count_].topic, sizeof(subs_[sub_count_].topic),
                     "%s", topic);
            subs_[sub_count_].qos = qos;
            sub_count_++;
        }
    }

    // Отправляем SUBSCRIBE только если уже подключены (иначе — при CONNECTED)
    if (!connected_) return 0;

    return esp_mqtt_client_subscribe(
        (esp_mqtt_client_handle_t)client_, topic, (int)qos);
}

int MqttClientAdapter::unsubscribe(const char* topic)
{
    if (!client_) return -1;
    return esp_mqtt_client_unsubscribe(
        (esp_mqtt_client_handle_t)client_, topic);
}

void MqttClientAdapter::set_event_callback(EventCallback cb, void* user_ctx)
{
    user_cb_ = cb;
    user_ctx_ = user_ctx;
}

// ── Управление реконнектом ──────────────────────────────────

bool MqttClientAdapter::should_reconnect(uint64_t now_us) const
{
    return !connected_ && (now_us >= reconnect_after_us_);
}

void MqttClientAdapter::reset_backoff()
{
    reconnect_delay_s_ = 1;
    reconnect_after_us_ = 0;
}

void MqttClientAdapter::schedule_reconnect(uint64_t now_us)
{
    reconnect_after_us_ = now_us + (uint64_t)reconnect_delay_s_ * 1'000'000ULL;
    ESP_LOGI(TAG, "Реконнект через %d с", reconnect_delay_s_);
    if (reconnect_delay_s_ < 60) {
        reconnect_delay_s_ = std::min(reconnect_delay_s_ * 2, 60);
    }
}

// ── Обработчик событий ESP-MQTT ─────────────────────────────

void MqttClientAdapter::esp_event_handler(void* handler_args,
                                           const char* /*event_base*/,
                                           int32_t event_id,
                                           void* event_data)
{
    auto* self = static_cast<MqttClientAdapter*>(handler_args);
    self->on_event((int)event_id, event_data);
}

void MqttClientAdapter::on_event(int event_id, void* event_data)
{
    switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Подключён к брокеру");
        connected_ = true;

        // Переподписаться на все топики
        for (int i = 0; i < sub_count_; i++) {
            esp_mqtt_client_subscribe(
                (esp_mqtt_client_handle_t)client_,
                subs_[i].topic, (int)subs_[i].qos);
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        ESP_LOGW(TAG, "Отключён от брокера");
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

            // ТОЛЬКО помещаем в очередь, НЕ обрабатываем
            if (!sink_.push(msg)) {
                ESP_LOGW(TAG, "Очередь команд MQTT переполнена — сообщение отброшено");
            }
        }
        break;
    }
    case MQTT_EVENT_ERROR: {
        ESP_LOGE(TAG, "Ошибка MQTT");
        connected_ = false;
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
