#include "infrastructure/driving/mqtt_interactor.h"

#include "application/ports/driven/imqtt_hardware.h"
#include "application/ports/driven/imqtt_message_sink.h"
#include "application/ports/driven/imqtt_config_persistence.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driving/iconfigure_system.h"
#include "application/ports/driven/imqtt_state_renderer.h"
#include "infrastructure/driving/json_helpers.h"
#include "mqtt_client.h"
#include "esp_system.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

// ── Конструктор ──────────────────────────────────────────────

MqttInteractor::MqttInteractor(
    IMqttHardware& mqtt, IMqttMessageSink& sink,
    IMqttConfigPersistence& cfg_store,
    IHeatingStateStore& state,
    IConfigureSystem& cfg_sys,
    ILogger& log, ITimeSource& time,
    IMqttStateRenderer& renderer)
    : mqtt_(mqtt), sink_(sink), cfg_store_(cfg_store),
      state_(state), cfg_sys_(cfg_sys),
      log_(log), time_(time), renderer_(renderer)
{}

// ── init ─────────────────────────────────────────────────────

void MqttInteractor::init()
{
    // Значения по умолчанию
    snprintf(host_, sizeof(host_), "%s", "");
    port_ = 1883;
    snprintf(user_, sizeof(user_), "%s", "");
    snprintf(pass_, sizeof(pass_), "%s", "");
    snprintf(prefix_, sizeof(prefix_), "%s", "esp-ot-gateway");
    enabled_ = false;
    tls_ = false;

    if (!cfg_store_.load_mqtt_config(host_, sizeof(host_), port_,
                                      user_, sizeof(user_),
                                      pass_, sizeof(pass_),
                                      prefix_, sizeof(prefix_),
                                      enabled_, tls_)) {
        // NVS не настроен — оставляем значения по умолчанию
    }

    if (!enabled_) {
        mqtt_state_ = State::DISABLED;
        return;
    }

    mqtt_.set_event_callback(mqtt_callback, this);
    connect_to_broker();
}

// ── poll ─────────────────────────────────────────────────────

void MqttInteractor::poll()
{
    if (mqtt_state_ == State::DISABLED) return;

    uint64_t now_us = time_.monotonic_us();

    // Реконнект с экспоненциальным backoff
    if (mqtt_state_ == State::DISCONNECTED
        && last_reconnect_attempt_us_ != 0
        && now_us >= last_reconnect_attempt_us_) {
        log_.event(ILogger::SYSTEM, "MQTT: реконнект (задержка %d с)...",
                   reconnect_delay_s_);
        connect_to_broker();
        // connect_to_broker обновит mqtt_state_ и schedule_reconnect при неудаче
    }

    // Обработка очереди команд
    drain_queue();

    // Применить отложенные действия из MQTT-колбека
    if (pending_state_update_) {
        pending_state_update_ = false;
        state_.set_mqtt_connected(pending_connected_);
        if (pending_connected_) {
            log_.event(ILogger::SYSTEM, "MQTT: подключён к %s", host_);
        }
    }
    if (pending_error_) {
        pending_error_ = false;
        log_.event(ILogger::SYSTEM, "MQTT: ошибка клиента (event_id=0)");
    }
    if (pending_connected_publish_) {
        pending_connected_publish_ = false;
        publish_online();
    }

    poll_counter_++;

    if (mqtt_state_ == State::CONNECTED) {
        // Публикация статуса каждые PUBLISH_INTERVAL циклов
        if (poll_counter_ % PUBLISH_INTERVAL == 0) {
            publish_status();
        }
        // Публикация статистики каждые STATS_INTERVAL циклов
        stats_tick_++;
        if (stats_tick_ % STATS_INTERVAL == 0) {
            publish_stats();
        }
    }
}

// ── save_and_apply (IMqttConfigurator) ───────────────────────

void MqttInteractor::save_and_apply(const char* host, uint16_t port,
                                     const char* user, const char* pass,
                                     const char* prefix, bool enabled, bool tls)
{
    snprintf(host_, sizeof(host_), "%s", host ? host : "");
    port_ = port;
    snprintf(user_, sizeof(user_), "%s", user ? user : "");
    snprintf(pass_, sizeof(pass_), "%s", pass ? pass : "");
    snprintf(prefix_, sizeof(prefix_), "%s", prefix ? prefix : "");
    enabled_ = enabled;
    tls_ = tls;

    cfg_store_.save_mqtt_config(host_, port_, user_, pass_, prefix_, enabled_, tls_);

    // Пересоздание MQTT-клиента при работающем HTTP-сервере
    // истощает сокеты. Сохраняем настройки и перезагружаемся.
    log_.event(ILogger::USER, "MQTT: настройки изменены, перезагрузка...");
    esp_restart();
}

// ── is_connected ─────────────────────────────────────────────

bool MqttInteractor::is_connected() const
{
    return mqtt_state_ == State::CONNECTED && mqtt_.is_connected();
}

// ── connect_to_broker ────────────────────────────────────────

void MqttInteractor::connect_to_broker()
{
    mqtt_state_ = State::CONNECTING;

    char uri[BUF_URI];
    build_uri(uri, sizeof(uri));

    char lwt[128];
    snprintf(lwt, sizeof(lwt), "%s/online", prefix_);

    const char* u = (user_[0] != '\0') ? user_ : nullptr;
    const char* p = (pass_[0] != '\0') ? pass_ : nullptr;

    if (!mqtt_.connect(uri, u, p, lwt, "offline", true, 60)) {
        mqtt_state_ = State::DISCONNECTED;
        schedule_reconnect();
        log_.event(ILogger::SYSTEM, "MQTT: ошибка подключения к %s", host_);
        return;
    }

    // Подписаться на командный топик
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/cmd/control", prefix_);
    mqtt_.subscribe(topic, IMqttHardware::QoS::AT_LEAST_ONCE);

    log_.event(ILogger::SYSTEM, "MQTT: подключение к %s", host_);
}

void MqttInteractor::schedule_reconnect()
{
    last_reconnect_attempt_us_ = time_.monotonic_us()
        + (uint64_t)reconnect_delay_s_ * 1'000'000ULL;
    if (reconnect_delay_s_ < 60) {
        reconnect_delay_s_ = std::min(reconnect_delay_s_ * 2, 60);
    }
}

void MqttInteractor::build_uri(char* buf, size_t size)
{
    snprintf(buf, size, "%s://%s:%u", tls_ ? "mqtts" : "mqtt", host_, port_);
}

// ── mqtt_callback ────────────────────────────────────────────

void MqttInteractor::mqtt_callback(int event_id, void* /*event_data*/, void* user_ctx)
{
    auto* self = static_cast<MqttInteractor*>(user_ctx);
    // Запущен из MQTT-задачи — НЕ трогаем state_, log_, time_.
    // Только обновляем внутренние флаги; обработка в poll().

    if (event_id == MQTT_EVENT_ERROR) {
        self->pending_error_ = true;
    }
    else if (event_id == MQTT_EVENT_CONNECTED) {
        self->mqtt_state_ = State::CONNECTED;
        self->reconnect_delay_s_ = 1;
        self->last_reconnect_attempt_us_ = 0;

        // ВСЕ публикации — в poll(). Здесь только флаги.
        self->pending_state_update_ = true;
        self->pending_connected_ = true;
        self->pending_connected_publish_ = true;
    }
    else if (event_id == MQTT_EVENT_DISCONNECTED) {
        self->mqtt_state_ = State::DISCONNECTED;
        self->schedule_reconnect();

        // Отложенное обновление state store — обработается в poll()
        self->pending_state_update_ = true;
        self->pending_connected_ = false;
    }
}

// ── drain_queue ──────────────────────────────────────────────

void MqttInteractor::drain_queue()
{
    IMqttMessageSink::Message msg;
    while (sink_.pop(msg)) {
        process_message(msg);
    }
}

void MqttInteractor::process_message(const IMqttMessageSink::Message& msg)
{
    // Найти суффикс топика (после prefix_)
    const char* cmd = strstr(msg.topic, "/cmd/");
    if (!cmd) return;

    cmd += 5;  // пропустить "/cmd/"

    if (strcmp(cmd, "control") == 0) {
        handle_control(msg.payload, msg.payload_len);
    }
}

// ── handle_control ───────────────────────────────────────────
// Принимает только dhw_enable — управление БКН (вкл/выкл).

void MqttInteractor::handle_control(const char* body, int /*len*/)
{
    int v = json_get_int(body, "\"dhw_enable\"");
    if (v >= 0) {
        cfg_sys_.set_dhw_enable(v != 0);
        // Немедленная публикация статуса после команды
        if (mqtt_state_ == State::CONNECTED) publish_status();
        log_.event(ILogger::USER, "MQTT: БКН %s", v ? "включён" : "выключен");
    }
}

// ── publish_status ───────────────────────────────────────────

void MqttInteractor::publish_status()
{
    static char buf[BUF_STATUS];
    renderer_.render_status(buf, sizeof(buf));
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/status", prefix_);
    mqtt_.publish(topic, buf, -1, IMqttHardware::QoS::AT_LEAST_ONCE, false);
}

// ── publish_stats ────────────────────────────────────────────

void MqttInteractor::publish_stats()
{
    static char buf[BUF_STATUS];
    renderer_.render_stats(buf, sizeof(buf));
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/stats", prefix_);
    mqtt_.publish(topic, buf, -1, IMqttHardware::QoS::AT_LEAST_ONCE, false);
}

// ── publish_online ───────────────────────────────────────────

void MqttInteractor::publish_online()
{
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/online", prefix_);
    mqtt_.publish(topic, "online", -1, IMqttHardware::QoS::AT_LEAST_ONCE, true);
}
