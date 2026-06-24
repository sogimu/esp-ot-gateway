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
        // Авто-публикация HA discovery при первом подключении
        if (!ha_discovery_published_) {
            publish_all_ha_discovery();
            ha_discovery_published_ = true;
            ha_discovery_last_us_ = now_us;
        }
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

    // Подписаться на командные топики
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/cmd/control", prefix_);
    mqtt_.subscribe(topic, IMqttHardware::QoS::AT_LEAST_ONCE);
    snprintf(topic, sizeof(topic), "%s/cmd/ha_discovery", prefix_);
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
    } else if (strcmp(cmd, "ha_discovery") == 0) {
        handle_ha_discovery_trigger();
    }
}

// ── handle_control ───────────────────────────────────────────

void MqttInteractor::handle_control(const char* body, int /*len*/)
{
    int v;
    float f;
    bool changed = false;

    v = json_get_int(body, "\"dhw_enable\"");
    if (v >= 0) {
        cfg_sys_.set_dhw_enable(v != 0);
        log_.event(ILogger::USER, "MQTT: БКН %s", v ? "включён" : "выключен");
        changed = true;
    }

    v = json_get_int(body, "\"ch_enable\"");
    if (v >= 0) {
        cfg_sys_.set_ch_enable(v != 0);
        log_.event(ILogger::USER, "MQTT: отопление %s", v ? "включено" : "выключено");
        changed = true;
    }

    f = json_get_float(body, "\"ch_setpoint\"");
    if (f > -1e37f) {
        if (f < 20) f = 20;
        if (f > 80) f = 80;
        cfg_sys_.set_ch_setpoint(f);
        log_.event(ILogger::USER, "MQTT: уставка СО = %.1f°C", (double)f);
        changed = true;
    }

    f = json_get_float(body, "\"dhw_setpoint\"");
    if (f > -1e37f) {
        if (f < 35) f = 35;
        if (f > 80) f = 80;
        cfg_sys_.set_dhw_setpoint(f);
        log_.event(ILogger::USER, "MQTT: уставка ГВС = %.1f°C", (double)f);
        changed = true;
    }

    if (changed && mqtt_state_ == State::CONNECTED) {
        publish_status();
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

// ── handle_ha_discovery_trigger ──────────────────────────────

void MqttInteractor::handle_ha_discovery_trigger()
{
    uint64_t now_us = time_.monotonic_us();
    if (ha_discovery_published_
        && now_us - ha_discovery_last_us_ < HA_REDISCOVERY_COOLDOWN_US) {
        log_.event(ILogger::USER, "MQTT: HA discovery пропущен (cooldown)");
        return;
    }
    publish_all_ha_discovery();
    ha_discovery_published_ = true;
    ha_discovery_last_us_ = now_us;
    log_.event(ILogger::USER, "MQTT: HA discovery опубликован");
}

// ── HA Discovery ─────────────────────────────────────────

char* MqttInteractor::build_ha_device_json(char* buf, size_t size)
{
    snprintf(buf, size,
        "{\"identifiers\":[\"esp-ot-gw-%s\"],"
        "\"name\":\"Контроллер котла\","
        "\"model\":\"ESP-OT-Gateway\",\"manufacturer\":\"Custom\"}",
        prefix_);
    return buf;
}

void MqttInteractor::publish_ha_config(const char* component, const char* entity,
                                        const char* json)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config",
             component, prefix_, entity);
    mqtt_.publish(topic, json, -1, IMqttHardware::QoS::AT_LEAST_ONCE, true);
}

void MqttInteractor::publish_ha_sensor(const char* entity, const char* name,
                                        const char* unit, const char* dev_class,
                                        const char* value_tpl)
{
    char dev_json[256];
    build_ha_device_json(dev_json, sizeof(dev_json));

    char buf[BUF_HA];
    int pos = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"uniq_id\":\"esp-ot-gw-%s_%s\","
        "\"stat_t\":\"%s/status\",\"val_tpl\":\"%s\","
        "\"dev\":%s,"
        "\"avty\":{\"t\":\"%s/online\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"}",
        name, prefix_, entity,
        prefix_, value_tpl,
        dev_json,
        prefix_);

    if (unit && unit[0] && pos > 0 && pos < (int)sizeof(buf) - 30) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",\"unit_of_meas\":\"%s\"", unit);
    }
    if (dev_class && dev_class[0] && pos > 0 && pos < (int)sizeof(buf) - 30) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",\"dev_cla\":\"%s\"", dev_class);
    }
    if (pos > 0 && pos < (int)sizeof(buf) - 10) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"qos\":1}");
    }

    publish_ha_config("sensor", entity, buf);
}

void MqttInteractor::publish_ha_binary_sensor(const char* entity, const char* name,
                                               const char* dev_class, const char* value_tpl)
{
    char dev_json[256];
    build_ha_device_json(dev_json, sizeof(dev_json));

    char buf[BUF_HA];
    int pos = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"uniq_id\":\"esp-ot-gw-%s_%s\","
        "\"stat_t\":\"%s/status\",\"val_tpl\":\"%s\","
        "\"dev\":%s,"
        "\"avty\":{\"t\":\"%s/online\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"}",
        name, prefix_, entity,
        prefix_, value_tpl,
        dev_json,
        prefix_);

    if (dev_class && dev_class[0] && pos > 0 && pos < (int)sizeof(buf) - 30) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",\"dev_cla\":\"%s\"", dev_class);
    }
    if (pos > 0 && pos < (int)sizeof(buf) - 10) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"qos\":1}");
    }

    publish_ha_config("binary_sensor", entity, buf);
}

void MqttInteractor::publish_ha_switch(const char* entity, const char* name,
                                        const char* icon, const char* state_tpl,
                                        const char* cmd_tpl)
{
    char dev_json[256];
    build_ha_device_json(dev_json, sizeof(dev_json));

    char buf[BUF_HA];
    int pos = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"uniq_id\":\"esp-ot-gw-%s_%s\","
        "\"stat_t\":\"%s/status\",\"val_tpl\":\"%s\","
        "\"cmd_t\":\"%s/cmd/control\",\"cmd_tpl\":\"%s\","
        "\"dev\":%s,"
        "\"avty\":{\"t\":\"%s/online\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"}",
        name, prefix_, entity,
        prefix_, state_tpl,
        prefix_, cmd_tpl,
        dev_json,
        prefix_);

    if (icon && icon[0] && pos > 0 && pos < (int)sizeof(buf) - 30) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",\"ic\":\"%s\"", icon);
    }
    if (pos > 0 && pos < (int)sizeof(buf) - 10) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"qos\":1}");
    }

    publish_ha_config("switch", entity, buf);
}

void MqttInteractor::publish_ha_number(const char* entity, const char* name,
                                        float min_v, float max_v, float step,
                                        const char* unit, const char* state_tpl,
                                        const char* cmd_tpl)
{
    char dev_json[256];
    build_ha_device_json(dev_json, sizeof(dev_json));

    char buf[BUF_HA];
    int pos = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"uniq_id\":\"esp-ot-gw-%s_%s\","
        "\"stat_t\":\"%s/status\",\"val_tpl\":\"%s\","
        "\"cmd_t\":\"%s/cmd/control\",\"cmd_tpl\":\"%s\","
        "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,"
        "\"dev\":%s,"
        "\"avty\":{\"t\":\"%s/online\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"}",
        name, prefix_, entity,
        prefix_, state_tpl,
        prefix_, cmd_tpl,
        (double)min_v, (double)max_v, (double)step,
        dev_json,
        prefix_);

    if (unit && unit[0] && pos > 0 && pos < (int)sizeof(buf) - 30) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",\"unit_of_meas\":\"%s\"", unit);
    }
    if (pos > 0 && pos < (int)sizeof(buf) - 10) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"qos\":1}");
    }

    publish_ha_config("number", entity, buf);
}

void MqttInteractor::publish_all_ha_discovery()
{
    // Датчики температуры (9)
    publish_ha_sensor("ch_temp",     "Температура СО",   "°C", "temperature",  "{{ value_json.ch_temp }}");
    publish_ha_sensor("dhw_temp",    "Температура ГВС",  "°C", "temperature",  "{{ value_json.dhw_temp }}");
    publish_ha_sensor("return_temp", "Обратка",          "°C", "temperature",  "{{ value_json.return_temp }}");
    publish_ha_sensor("outside_temp","Улица",            "°C", "temperature",  "{{ value_json.outside_temp }}");
    publish_ha_sensor("t1_temp",     "Комната T1",       "°C", "temperature",  "{{ value_json.t1_temp }}");
    publish_ha_sensor("t2_temp",     "Комната T2",       "°C", "temperature",  "{{ value_json.t2_temp }}");
    publish_ha_sensor("modulation",  "Модуляция",         "%", "power_factor", "{{ value_json.modulation }}");
    publish_ha_sensor("uptime",      "Аптайм",            "s", "duration",     "{{ value_json.uptime_sec }}");
    publish_ha_sensor("total_uptime","Общий аптайм",      "s", "duration",     "{{ value_json.total_uptime_sec }}");

    // Бинарные датчики (5)
    publish_ha_binary_sensor("flame",      "Пламя",         "heat",         "{{ value_json.flame == 1 }}");
    publish_ha_binary_sensor("fault",      "Ошибка",        "problem",      "{{ value_json.fault == 1 }}");
    publish_ha_binary_sensor("ch_active",  "СО активна",    "running",      "{{ value_json.ch_active == 1 }}");
    publish_ha_binary_sensor("dhw_active", "ГВС активно",   "running",      "{{ value_json.dhw_active == 1 }}");
    publish_ha_binary_sensor("connected",  "Котёл",         "connectivity", "{{ value_json.connected == 1 }}");

    // Переключатели (2)
    publish_ha_switch("ch_enable",  "Отопление", "mdi:radiator",
        "{{ value_json.ch_enable == 1 }}",
        "{\\\"ch_enable\\\":{{ value == \\\"ON\\\" | int }}}");
    publish_ha_switch("dhw_enable", "ГВС",       "mdi:water-boiler",
        "{{ value_json.dhw_enable == 1 }}",
        "{\\\"dhw_enable\\\":{{ value == \\\"ON\\\" | int }}}");

    // Числовые параметры (2)
    publish_ha_number("ch_setpoint",  "Уставка СО",  20, 80, 1, "°C",
        "{{ value_json.ch_setpoint }}",  "{\\\"ch_setpoint\\\":{{ value }}}");
    publish_ha_number("dhw_setpoint", "Уставка ГВС", 35, 80, 1, "°C",
        "{{ value_json.dhw_setpoint }}", "{\\\"dhw_setpoint\\\":{{ value }}}");
}
