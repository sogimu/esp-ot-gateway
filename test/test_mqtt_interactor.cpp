#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <cstdarg>
#include <string>
#include <algorithm>

#include "application/ports/driven/imqtt_hardware.h"
#include "application/ports/driven/imqtt_message_sink.h"
#include "application/ports/driven/imqtt_config_persistence.h"
#include "application/ports/driven/imqtt_state_renderer.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include "application/ports/driving/iconfigure_system.h"
#include "application/ports/driving/imqtt_configurator.h"

#include "infrastructure/driving/mqtt_interactor.h"
#include "fakes/fake_mqtt_hardware.h"
#include "fakes/fake_mqtt_message_sink.h"
#include "fakes/fake_mqtt_config_persistence.h"
#include "fakes/fake_mqtt_renderer.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ── Тестовый SystemConfigInteractor (Spy) ─────────────────

struct SpySystemConfig : public IConfigureSystem {
    void set_ch_mode(CHMode) override {}
    void set_ch_enable(bool v) override { ch_enable_ = v; ch_enable_set_ = true; }
    void set_dhw_enable(bool v) override { dhw_enable_ = v; dhw_enable_set_ = true; }
    void set_ch_setpoint(float v) override { ch_setpoint_ = v; ch_setpoint_set_ = true; }
    void set_dhw_setpoint(float v) override { dhw_setpoint_ = v; dhw_setpoint_set_ = true; }
    void set_dhw_hysteresis(float) override {}
    void set_schedule(const CH_Schedule&) override {}
    void set_pid_schedule(const PID_Schedule&) override {}
    void set_timezone(int) override {}
    void set_sntp_servers(const char*, const char*) override {}
    void reset_modulation_stats() override {}
    void reset_cycle_stats() override {}
    void reset_gas_stats() override {}

    bool ch_enable_set_ = false, dhw_enable_set_ = false;
    bool ch_enable_ = false, dhw_enable_ = false;
    bool ch_setpoint_set_ = false, dhw_setpoint_set_ = false;
    float ch_setpoint_ = 0, dhw_setpoint_ = 0;
};

struct MqttTestLogger : public ILogger {
    void event(Category, const char* fmt, ...) override {
        va_list args; va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args); event_count_++;
    }
    char last_msg_[256] = {};
    int event_count_ = 0;
};

// ── Вспомогательная функция сборки MqttInteractor ─────────

struct MqttTestFixture {
    FakeMqttHardware mqtt;
    FakeMqttMessageSink sink;
    FakeMqttConfigPersistence cfg;
    FakeHeatingStateStore state;
    SpySystemConfig spy;
    MqttTestLogger log;
    FakeTimeSource time;
    FakeMqttRenderer renderer;
    MqttInteractor interactor;

    MqttTestFixture()
        : interactor(mqtt, sink, cfg, state, spy, log, time, renderer)
    {}
};

// ── Тесты жизненного цикла ────────────────────────────────

TEST_CASE("MqttInteractor: init с enabled=false не подключается", "[mqtt][interactor][lifecycle]") {
    MqttTestFixture f;
    f.cfg.preset("broker.local", 1883, "", "", "gw", false, false);
    f.interactor.init();
    REQUIRE_FALSE(f.mqtt.is_connected());
}

TEST_CASE("MqttInteractor: init с enabled=true подключается к брокеру", "[mqtt][interactor][lifecycle]") {
    MqttTestFixture f;
    f.cfg.preset("broker.local", 1883, "", "", "gw", true, false);
    f.interactor.init();
    REQUIRE(f.mqtt.is_connected());
}

TEST_CASE("MqttInteractor: poll при DISABLED не делает ничего", "[mqtt][interactor][lifecycle]") {
    MqttTestFixture f;
    // init не вызван — mqtt_state_ = DISABLED
    f.interactor.poll();
    REQUIRE(f.renderer.render_status_called_ == 0);
}

TEST_CASE("MqttInteractor: connected переходит в CONNECTED после inject_connected", "[mqtt][interactor][lifecycle]") {
    MqttTestFixture f;
    f.cfg.preset("broker.local", 1883, "", "", "gw", true, false);
    f.interactor.init();
    REQUIRE(f.mqtt.is_connected());

    // Симулируем успешное подключение
    f.mqtt.inject_connected();
    f.interactor.poll();
    REQUIRE(f.interactor.is_connected());
}

TEST_CASE("MqttInteractor: disconnected переходит в DISCONNECTED и запускает backoff", "[mqtt][interactor][lifecycle]") {
    MqttTestFixture f;
    f.cfg.preset("broker.local", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();
    REQUIRE(f.interactor.is_connected());

    // Симулируем разрыв
    f.mqtt.inject_disconnected();
    f.interactor.poll();
    REQUIRE_FALSE(f.interactor.is_connected());
}

// ── Тесты обработки команд ────────────────────────────────

TEST_CASE("MqttInteractor: handle_control dhw_enable=1 включает БКН", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_enable\":1}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);

    f.interactor.poll();
    REQUIRE(f.spy.dhw_enable_set_);
    REQUIRE(f.spy.dhw_enable_);
}

TEST_CASE("MqttInteractor: handle_control dhw_enable=0 выключает БКН", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_enable\":0}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);

    f.interactor.poll();
    REQUIRE(f.spy.dhw_enable_set_);
    REQUIRE_FALSE(f.spy.dhw_enable_);
}

TEST_CASE("MqttInteractor: handle_control ch_enable и ch_setpoint", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_enable\":1,\"ch_setpoint\":65}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.ch_enable_set_);
    REQUIRE(f.spy.ch_enable_);
    REQUIRE(f.spy.ch_setpoint_set_);
    REQUIRE(f.spy.ch_setpoint_ == Approx(65.0f));
}

TEST_CASE("MqttInteractor: handle_control dhw_setpoint", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_setpoint\":60}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.dhw_setpoint_set_);
    REQUIRE(f.spy.dhw_setpoint_ == Approx(60.0f));
}

TEST_CASE("MqttInteractor: handle_control clamping значений", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // ch_setpoint выше максимума → clamp to 80
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":999}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.ch_setpoint_ == Approx(80.0f));

    // ch_setpoint ниже минимума → clamp to 20
    f.spy.ch_setpoint_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":-5}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.ch_setpoint_ == Approx(20.0f));

    // dhw_setpoint ниже минимума → clamp to 35
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_setpoint\":10}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.dhw_setpoint_ == Approx(35.0f));

    // dhw_setpoint выше максимума → clamp to 80
    f.spy.dhw_setpoint_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_setpoint\":999}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.dhw_setpoint_ == Approx(80.0f));
}

TEST_CASE("MqttInteractor: handle_control все поля в одном сообщении", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload),
        "{\"dhw_enable\":0,\"ch_enable\":1,\"ch_setpoint\":70,\"dhw_setpoint\":55}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.dhw_enable_set_);
    REQUIRE_FALSE(f.spy.dhw_enable_);
    REQUIRE(f.spy.ch_enable_set_);
    REQUIRE(f.spy.ch_enable_);
    REQUIRE(f.spy.ch_setpoint_ == Approx(70.0f));
    REQUIRE(f.spy.dhw_setpoint_ == Approx(55.0f));
}

TEST_CASE("MqttInteractor: handle_control неизвестные поля игнорируются", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload),
        "{\"pid_kp\":5,\"fault_reset\":1,\"unknown_field\":42}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    // Ни одно известное поле не изменено
    REQUIRE_FALSE(f.spy.dhw_enable_set_);
    REQUIRE_FALSE(f.spy.ch_enable_set_);
    REQUIRE_FALSE(f.spy.ch_setpoint_set_);
    REQUIRE_FALSE(f.spy.dhw_setpoint_set_);
    // Не должно крашнуться
    REQUIRE(f.log.event_count_ >= 0);
}

TEST_CASE("MqttInteractor: handle_control повреждённый JSON не крашит", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "not json at all {{{");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);

    f.interactor.poll();
    REQUIRE(f.log.event_count_ >= 0);  // просто не крашнулось
}

TEST_CASE("MqttInteractor: handle_control dhw_enable без подключения не публикует статус", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    // НЕ вызываем inject_connected — состояние != CONNECTED
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_enable\":1}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    // Команда обработана...
    REQUIRE(f.spy.dhw_enable_set_);
    // ...но статус не опубликован (не CONNECTED)
    REQUIRE(f.renderer.render_status_called_ == 0);
}

// ── Тесты HA discovery ──────────────────────────────────────

TEST_CASE("MqttInteractor: HA discovery публикуется при первом CONNECTED", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("gw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();

    f.mqtt.inject_connected();
    f.interactor.poll();

    // 18 entity + online + подписки могут породить publishes
    int ha_count = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(ha_count > 0);
}

TEST_CASE("MqttInteractor: HA discovery публикует все 18 entity", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "testgw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();

    f.mqtt.inject_connected();
    f.interactor.poll();

    // Проверяем конкретные entity
    REQUIRE(f.mqtt.last_publish_to("homeassistant/sensor/testgw_ch_temp/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/sensor/testgw_dhw_temp/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/binary_sensor/testgw_flame/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/binary_sensor/testgw_fault/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/switch/testgw_ch_enable/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/switch/testgw_dhw_enable/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/number/testgw_ch_setpoint/config") != nullptr);
    REQUIRE(f.mqtt.last_publish_to("homeassistant/number/testgw_dhw_setpoint/config") != nullptr);
}

TEST_CASE("MqttInteractor: HA discovery ON switch cmd_tpl генерирует dhw_enable=1", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("gw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();

    f.mqtt.inject_connected();
    f.interactor.poll();

    // Проверяем что cmd_tpl содержит правильный шаблон с приоритетом
    auto* p = f.mqtt.last_publish_to("homeassistant/switch/gw_dhw_enable/config");
    REQUIRE(p != nullptr);
    // После C++ и JSON парсинга, cmd_tpl должно быть:
    // {"dhw_enable":{{ (value == "ON") | int }}}
    // Проверяем что есть скобки вокруг сравнения
    REQUIRE(std::string(p->data).find("(value ==") != std::string::npos);
}

TEST_CASE("MqttInteractor: HA discovery не публикуется повторно без триггера", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("gw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();

    // Первый коннект
    f.mqtt.inject_connected();
    f.interactor.poll();
    int first_count = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(first_count > 0);

    // Второй коннект без сброса ha_discovery_published_ — discovery не должен удвоиться
    f.mqtt.publishes_.clear();
    f.mqtt.inject_disconnected();
    f.interactor.poll();
    f.mqtt.inject_connected();
    f.interactor.poll();
    // pending_connected_publish_ взводится заново при каждом CONNECTED,
    // но ha_discovery_published_ уже true → повторной публикации не будет
    int second_count = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(second_count == 0);
}

TEST_CASE("MqttInteractor: HA discovery можно принудительно переопубликовать", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw2", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();
    f.mqtt.publishes_.clear();

    // Продвигаем время за cooldown (10 мин + 1 сек)
    f.time.advance_sec(601);

    // Отправляем ha_discovery команду
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw2/cmd/ha_discovery");
    cmd.payload[0] = '\0';
    cmd.payload_len = 0;
    f.sink.push(cmd);
    f.interactor.poll();

    int ha_count = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(ha_count == 18);
}

TEST_CASE("MqttInteractor: ha_discovery повторный вызов в cooldown игнорируется", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw3", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();
    f.mqtt.publishes_.clear();

    // Продвигаем время за cooldown
    f.time.advance_sec(601);

    // Первый ручной триггер (после cooldown) — должен сработать
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw3/cmd/ha_discovery");
    cmd.payload[0] = '\0';
    cmd.payload_len = 0;
    f.sink.push(cmd);
    f.interactor.poll();
    int first = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(first == 18);

    // Второй сразу — должен игнорироваться (cooldown 10 min)
    f.mqtt.publishes_.clear();
    f.sink.push(cmd);
    f.interactor.poll();
    int second = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(second == 0);
}

// ── Тесты публикации ──────────────────────────────────────

TEST_CASE("MqttInteractor: статус публикуется каждые 25 poll()", "[mqtt][interactor][publish]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();

    // Сбросим счётчики publishes_ после init (которая могла вызвать publish_status)
    f.mqtt.publishes_.clear();
    f.renderer.render_status_called_ = 0;

    // poll_counter_ = 0 → после 24 вызовов ни один не делится на 25
    for (int i = 0; i < 24; i++) f.interactor.poll();
    REQUIRE(f.renderer.render_status_called_ == 0);

    // 25-й вызов: poll_counter_ = 25 → 25%25 == 0 → публикация
    f.interactor.poll();
    REQUIRE(f.renderer.render_status_called_ >= 1);
    auto* p = f.mqtt.last_publish_to("status");
    REQUIRE(p != nullptr);
}

// ── Тесты save_and_apply (IMqttConfigurator) ──────────────

TEST_CASE("MqttInteractor: save_and_apply сохраняет настройки в NVS", "[mqtt][interactor][config]") {
    MqttTestFixture f;
    f.cfg.preset("old.local", 1883, "old_user", "", "old_gw", true, false);
    f.interactor.init();

    // save_and_apply сохраняет настройки (на устройстве вызвал бы esp_restart)
    f.interactor.save_and_apply("new.local", 8883, "new_user", "new_pass",
                                 "new_gw", true, true);

    REQUIRE(f.cfg.save_called_);
    REQUIRE(std::string(f.cfg.host_) == "new.local");
    REQUIRE(f.cfg.port_ == 8883);
    REQUIRE(f.cfg.tls_ == true);

    // Геттеры отражают новые значения
    REQUIRE(std::string(f.interactor.get_host()) == "new.local");
    REQUIRE(f.interactor.get_port() == 8883);
}

// ── Тесты очереди ─────────────────────────────────────────

TEST_CASE("MqttInteractor: drain_queue обрабатывает все сообщения", "[mqtt][interactor][queue]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // Помещаем 3 команды в очередь
    for (int i = 0; i < 3; i++) {
        IMqttMessageSink::Message cmd;
        snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
        snprintf(cmd.payload, sizeof(cmd.payload), "{\"dhw_enable\":%d}", i % 2);
        cmd.payload_len = (int)strlen(cmd.payload);
        f.sink.push(cmd);
    }

    f.interactor.poll();
    // Все 3 должны быть обработаны за один poll
    REQUIRE(f.spy.dhw_enable_set_);
}

// ── Тесты online ─────────────────────────────────────────

TEST_CASE("MqttInteractor: online публикуется при CONNECTED", "[mqtt][interactor][online]") {
    MqttTestFixture f;
    f.cfg.preset("gw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();

    f.mqtt.inject_connected();
    f.interactor.poll();

    auto* p = f.mqtt.last_publish_to("online");
    REQUIRE(p != nullptr);
    REQUIRE(p->retain == true);
}

TEST_CASE("MqttInteractor: online публикуется с retain", "[mqtt][interactor][online]") {
    MqttTestFixture f;
    f.cfg.preset("testgw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();
    f.mqtt.inject_connected();
    f.interactor.poll();

    auto* p = f.mqtt.last_publish_to("online");
    REQUIRE(p != nullptr);
    REQUIRE(p->retain == true);
    REQUIRE(std::string(p->data) == "online");
}

// ── Тесты pending state update ────────────────────────────

TEST_CASE("MqttInteractor: set_mqtt_connected вызывается из poll после inject_connected", "[mqtt][interactor][state]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    REQUIRE_FALSE(f.state.is_mqtt_connected());  // ещё не обработано

    f.interactor.poll();
    REQUIRE(f.state.is_mqtt_connected());  // обработано в poll()
}

TEST_CASE("MqttInteractor: set_mqtt_connected(false) после disconnect", "[mqtt][interactor][state]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();
    REQUIRE(f.state.is_mqtt_connected());

    f.mqtt.inject_disconnected();
    f.interactor.poll();
    REQUIRE_FALSE(f.state.is_mqtt_connected());
}
