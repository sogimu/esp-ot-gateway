#pragma once

#include "application/ports/driving/ipollable.h"
#include "application/ports/driving/imqtt_configurator.h"
#include "application/ports/driven/imqtt_message_sink.h"
#include <cstdint>
#include <cstddef>

class IMqttHardware;
class IMqttMessageSink;
class IMqttConfigPersistence;
class IHeatingStateStore;
class IConfigureSystem;
class IConfigurePid;
class IGasCalibration;
class IFaultReset;
class IResetStatistics;
class ILogger;
class ITimeSource;
class IMqttStateRenderer;

/// Центральный компонент MQTT-логики.
///
/// Жизненный цикл:
///   1. init()         — загружает настройки из NVS, подключается (если enabled)
///   2. poll() ~1.1с   — периодическая публикация + обработка очереди команд
///
/// Входящие команды обрабатываются ИСКЛЮЧИТЕЛЬНО в poll().
/// MQTT-колбек только помещает сообщения в IMqttMessageSink.
class MqttInteractor : public IPollable, public IMqttConfigurator {
public:
    MqttInteractor(IMqttHardware& mqtt, IMqttMessageSink& sink,
                   IMqttConfigPersistence& cfg_store,
                   IHeatingStateStore& state,
                   IConfigureSystem& cfg_sys, IConfigurePid& cfg_pid,
                   IGasCalibration& gas, IFaultReset& fault,
                   IResetStatistics& reset, ILogger& log,
                   ITimeSource& time, IMqttStateRenderer& renderer);

    /// Загрузить настройки MQTT из NVS и подключиться (если enabled).
    void init();

    /// IPollable: периодический цикл (~1.1с).
    void poll() override;

    // ── IMqttConfigurator ───────────────────────────────

    bool is_enabled() const override      { return enabled_; }
    const char* get_host() const override { return host_; }
    uint16_t get_port() const override    { return port_; }
    const char* get_user() const override { return user_; }
    const char* get_prefix() const override { return prefix_; }
    bool get_tls() const override         { return tls_; }
    bool is_connected() const override;
    void save_and_apply(const char* host, uint16_t port,
                        const char* user, const char* pass,
                        const char* prefix, bool enabled, bool tls) override;

private:
    // ── Состояния ──────────────────────────────────────
    enum class State {
        DISABLED,       ///< MQTT выключен пользователем
        DISCONNECTED,   ///< Не подключён (ожидание backoff)
        CONNECTING,     ///< Попытка подключения
        CONNECTED       ///< Подключён и работает
    };

    // ── Подключение ────────────────────────────────────
    void connect_to_broker();
    void schedule_reconnect();
    void build_uri(char* buf, size_t size);

    // ── Обработка очереди команд ───────────────────────
    void drain_queue();
    void process_message(const IMqttMessageSink::Message& msg);

    // ── Обработчики команд ─────────────────────────────
    void handle_control(const char* payload, int len);
    void handle_schedule(const char* payload, int len, bool is_pid);
    void handle_ha_discovery_trigger();

    // ── Публикация ─────────────────────────────────────
    void publish_status();
    void publish_stats();
    void publish_online();

    // ── Home Assistant Auto-Discovery ──────────────────
    void publish_all_ha_discovery();
    void publish_ha_sensor(const char* entity, const char* name,
                           const char* unit, const char* dev_class,
                           const char* value_tpl);
    void publish_ha_binary_sensor(const char* entity, const char* name,
                                   const char* dev_class, const char* value_tpl);
    void publish_ha_switch(const char* entity, const char* name,
                            const char* icon, const char* state_tpl,
                            const char* cmd_tpl);
    void publish_ha_number(const char* entity, const char* name,
                            float min_v, float max_v, float step,
                            const char* unit, const char* state_tpl,
                            const char* cmd_tpl);
    void publish_ha_select(const char* entity, const char* name,
                            const char* options_json, const char* state_tpl,
                            const char* cmd_tpl);
    void publish_ha_climate();
    void publish_ha_config(const char* component, const char* entity,
                           const char* json);
    char* build_ha_device_json(char* buf, size_t size);

    // ── Колбек MQTT-клиента ────────────────────────────
    static void mqtt_callback(int event_id, void* event_data, void* user_ctx);

    // ── Зависимости (все по ссылке — без владения) ─────
    IMqttHardware&            mqtt_;
    IMqttMessageSink&         sink_;
    IMqttConfigPersistence&   cfg_store_;
    IHeatingStateStore&       state_;
    IConfigureSystem&         cfg_sys_;
    IConfigurePid&            cfg_pid_;
    IGasCalibration&          gas_;
    IFaultReset&              fault_;
    IResetStatistics&         reset_;
    ILogger&                  log_;
    ITimeSource&              time_;
    IMqttStateRenderer&       renderer_;

    // ── Настройки из NVS ───────────────────────────────
    char     host_[128]   = {};
    uint16_t port_        = 1883;
    char     user_[64]    = {};
    char     pass_[64]    = {};
    char     prefix_[64]  = {};
    bool     enabled_     = false;
    bool     tls_         = false;

    // ── Состояние ──────────────────────────────────────
    State    mqtt_state_  = State::DISABLED;
    int      poll_counter_= 0;
    int      stats_tick_  = 0;
    int      reconnect_delay_s_ = 1;
    uint64_t last_reconnect_attempt_us_ = 0;
    bool     ha_discovery_published_ = false;
    uint64_t ha_discovery_last_us_ = 0;

    // Отложенные действия из MQTT-колбека → poll()
    bool     pending_state_update_ = false;
    bool     pending_connected_    = false;
    bool     pending_connected_publish_ = false;  // online + HA discovery

    // ── Константы ──────────────────────────────────────
    static constexpr int BUF_STATUS = 2048;
    static constexpr int BUF_URI    = 256;
    static constexpr int BUF_HA     = 1536;
    static constexpr uint64_t HA_REDISCOVERY_COOLDOWN_US = 600'000'000; // 10 минут
    static constexpr int PUBLISH_INTERVAL = 5;   // публикация статуса каждые N циклов
    static constexpr int STATS_INTERVAL  = 55;   // публикация статистики каждые N циклов
};
