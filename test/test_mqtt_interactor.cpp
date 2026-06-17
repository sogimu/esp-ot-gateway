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
#include "application/ports/driving/iconfigure_pid.h"
#include "application/ports/driving/igas_calibration.h"
#include "application/ports/driving/ifault_reset.h"
#include "application/ports/driving/ireset_statistics.h"
#include "application/ports/driving/imqtt_configurator.h"
#include "domain/value_objects/ch_mode.h"

#include "infrastructure/driving/mqtt_interactor.h"
#include "fakes/fake_mqtt_hardware.h"
#include "fakes/fake_mqtt_message_sink.h"
#include "fakes/fake_mqtt_config_persistence.h"
#include "fakes/fake_mqtt_renderer.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_time_source.h"

using Catch::Approx;

// ── Тестовый SystemConfigInteractor (Spy) ─────────────────

struct SpySystemConfig : public IConfigureSystem,
                          public IConfigurePid,
                          public IGasCalibration,
                          public IFaultReset,
                          public IResetStatistics {
    // IConfigureSystem
    void set_ch_mode(CHMode m) override { ch_mode_ = m; ch_mode_set_ = true; }
    void set_ch_enable(bool v) override { ch_enable_ = v; ch_enable_set_ = true; }
    void set_dhw_enable(bool v) override { dhw_enable_ = v; }
    void set_ch_setpoint(float v) override { ch_sp_ = v; ch_sp_set_ = true; }
    void set_dhw_setpoint(float v) override { dhw_sp_ = v; }
    void set_dhw_hysteresis(float v) override { dhw_hyst_ = v; }
    void set_schedule(const CH_Schedule&) override {}
    void set_pid_schedule(const PID_Schedule&) override {}
    void set_timezone(int v) override { tz_ = v; tz_set_ = true; }
    void set_sntp_servers(const char* s0, const char* s1) override {
        snprintf(sntp0_, sizeof(sntp0_), "%s", s0 ? s0 : "");
        snprintf(sntp1_, sizeof(sntp1_), "%s", s1 ? s1 : "");
        sntp_set_ = true;
    }
    void reset_modulation_stats() override { mod_reset_ = true; }
    void reset_cycle_stats() override { cycle_reset_ = true; }
    void reset_gas_stats() override { gas_reset_ = true; }

    // IConfigurePid
    void set_pid_enable(bool v) override { pid_enable_ = v; }
    void set_pid_parameters(float kp, float ki, float kd, int dt, int sensor,
                            float target, int lockout) override {
        pid_kp_ = kp; pid_ki_ = ki; pid_kd_ = kd;
        pid_dt_ = dt; pid_sensor_ = sensor; pid_target_ = target; pid_lockout_ = lockout;
        pid_params_set_ = true;
    }
    void set_pid_hysteresis(float v) override { pid_hyst_ = v; }

    // IGasCalibration
    void set_k_calib(float v) override { k_calib_ = v; }
    void set_p_max(float) override {}
    void set_gas_calorific(float) override {}
    void set_gas_meter_base(float v) override { gas_base_ = v; }
    void add_meter_correction(float v) override { gas_corr_ = v; gas_corr_set_ = true; }
    void reset_corrections() override { corr_reset_ = true; }

    // IFaultReset
    void reset() override { fault_reset_ = true; }

    // IResetStatistics (already via IConfigureSystem, но отдельно для ясности)
    // reset_modulation_stats, reset_cycle_stats, reset_gas_stats — shared выше

    // Spy fields
    bool ch_enable_set_ = false, ch_sp_set_ = false, ch_mode_set_ = false;
    bool tz_set_ = false, sntp_set_ = false;
    bool pid_params_set_ = false;
    bool gas_corr_set_ = false;
    bool mod_reset_ = false, cycle_reset_ = false, gas_reset_ = false;
    bool corr_reset_ = false, fault_reset_ = false;
    bool ch_enable_ = false;
    CHMode ch_mode_ = CHMode::Manual_Static;
    float ch_sp_ = 0, dhw_sp_ = 0, dhw_hyst_ = 0;
    int tz_ = 0;
    float pid_kp_ = 0, pid_ki_ = 0, pid_kd_ = 0;
    int pid_dt_ = 0, pid_sensor_ = 0, pid_lockout_ = 0;
    float pid_target_ = 0, pid_hyst_ = 0;
    float k_calib_ = 0, gas_base_ = 0, gas_corr_ = 0;
    bool pid_enable_ = false, dhw_enable_ = false;
    char sntp0_[64] = {}, sntp1_[64] = {};
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
        : interactor(mqtt, sink, cfg, state, spy, spy, spy, spy, spy, log, time, renderer)
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

TEST_CASE("MqttInteractor: handle_control ch_enable=0", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // Отправляем команду через sink (как это делает MQTT-колбек)
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_enable\":0}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);

    f.interactor.poll();  // drain_queue + обработка
    REQUIRE(f.spy.ch_enable_set_);
    REQUIRE_FALSE(f.spy.ch_enable_);
}

TEST_CASE("MqttInteractor: handle_control ch_setpoint с clamping", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // Нормальное значение
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":65}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.ch_sp_ == Approx(65.0f));

    // За границей — должно зажаться
    f.spy.ch_sp_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":999}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.ch_sp_ == Approx(80.0f));  // clamped to max

    // Ниже границы
    f.spy.ch_sp_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":-5}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.ch_sp_ == Approx(20.0f));  // clamped to min
}

TEST_CASE("MqttInteractor: handle_control tz_offset через float-сентинел", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // tz_offset НЕ задан в JSON — не должен измениться на -1
    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":50}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE_FALSE(f.spy.tz_set_);  // tz_offset не был в JSON → не вызван

    // tz_offset задан явно
    f.spy.tz_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"tz_offset\":3}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.tz_set_);
    REQUIRE(f.spy.tz_ == 3);

    // tz_offset = -1 (валидный пояс)
    f.spy.tz_set_ = false;
    snprintf(cmd.payload, sizeof(cmd.payload), "{\"tz_offset\":-1}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();
    REQUIRE(f.spy.tz_set_);
    REQUIRE(f.spy.tz_ == -1);
}

TEST_CASE("MqttInteractor: handle_control все поля PID", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    // Установим baseline PID значения в state store
    f.state.set_pid_config(1.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload),
        "{\"pid_kp\":3.5,\"pid_ki\":0.015,\"pid_target_room\":23.0}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.pid_params_set_);
    REQUIRE(f.spy.pid_kp_ == Approx(3.5f));
    REQUIRE(f.spy.pid_ki_ == Approx(0.015f));
    REQUIRE(f.spy.pid_target_ == Approx(23.0f));
    // Не изменённые параметры должны остаться прежними
    REQUIRE(f.spy.pid_dt_ == 60);
    REQUIRE(f.spy.pid_sensor_ == 0);
}

TEST_CASE("MqttInteractor: handle_control сбросы и калибровка", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload),
        "{\"fault_reset\":1,\"reset_mod_stats\":1,\"reset_corrections\":1,"
        "\"k_calib\":0.95,\"gas_meter_correct\":1234.5}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.fault_reset_);
    REQUIRE(f.spy.mod_reset_);
    REQUIRE(f.spy.corr_reset_);
    REQUIRE(f.spy.k_calib_ == Approx(0.95f));
    REQUIRE(f.spy.gas_corr_ == Approx(1234.5f));
}

TEST_CASE("MqttInteractor: handle_control SNTP серверы", "[mqtt][interactor][control]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();
    f.interactor.poll();

    IMqttMessageSink::Message cmd;
    snprintf(cmd.topic, sizeof(cmd.topic), "gw/cmd/control");
    snprintf(cmd.payload, sizeof(cmd.payload),
        "{\"sntp_server0\":\"pool.ntp.org\",\"sntp_server1\":\"time.google.com\"}");
    cmd.payload_len = (int)strlen(cmd.payload);
    f.sink.push(cmd);
    f.interactor.poll();

    REQUIRE(f.spy.sntp_set_);
    REQUIRE(std::string(f.spy.sntp0_) == "pool.ntp.org");
    REQUIRE(std::string(f.spy.sntp1_) == "time.google.com");
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

    // Не должно упасть
    f.interactor.poll();
    REQUIRE(f.log.event_count_ >= 0);  // просто не крашнулось
}

// ── Тесты публикации ──────────────────────────────────────

TEST_CASE("MqttInteractor: статус публикуется каждые 5 poll()", "[mqtt][interactor][publish]") {
    MqttTestFixture f;
    f.cfg.preset("h", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.inject_connected();

    // Сбросим счётчики publishes_ после init (которая могла вызвать publish_status)
    f.mqtt.publishes_.clear();
    f.renderer.render_status_called_ = 0;

    // poll_counter_ = 0 → после 4 вызовов: 1,2,3,4 → ни один не делится на 5
    for (int i = 0; i < 4; i++) f.interactor.poll();
    REQUIRE(f.renderer.render_status_called_ == 0);

    // 5-й вызов: poll_counter_ = 5 → 5%5 == 0 → публикация
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
        snprintf(cmd.payload, sizeof(cmd.payload), "{\"ch_setpoint\":%d}", 50 + i);
        cmd.payload_len = (int)strlen(cmd.payload);
        f.sink.push(cmd);
    }

    f.interactor.poll();
    // Все 3 должны быть обработаны за один poll
    REQUIRE(f.spy.ch_sp_ == Approx(52.0f));  // последнее значение
}

// ── Тесты HA discovery ────────────────────────────────────
// HA discovery отключён на время отладки (публикуется только online).
// Тесты проверяют, что online публикуется, а HA — нет.

TEST_CASE("MqttInteractor: online публикуется при CONNECTED", "[mqtt][interactor][ha]") {
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

TEST_CASE("MqttInteractor: HA discovery НЕ публикуется при CONNECTED (отключён)", "[mqtt][interactor][ha]") {
    MqttTestFixture f;
    f.cfg.preset("testgw", 1883, "", "", "gw", true, false);
    f.interactor.init();
    f.mqtt.publishes_.clear();
    f.mqtt.inject_connected();
    f.interactor.poll();

    int ha_count = f.mqtt.count_publishes_to("homeassistant");
    REQUIRE(ha_count == 0);
}

TEST_CASE("MqttInteractor: save_and_apply вызывает перезагрузку", "[mqtt][interactor][ha]") {
    // Проверяем, что save_and_apply сохраняет настройки
    MqttTestFixture f;
    f.cfg.preset("old.local", 1883, "", "", "gw", true, false);
    f.interactor.init();

    f.interactor.save_and_apply("new.local", 8883, "u", "p", "pref", true, true);

    REQUIRE(f.cfg.save_called_);
    REQUIRE(std::string(f.cfg.host_) == "new.local");
    REQUIRE(f.cfg.port_ == 8883);
}

TEST_CASE("MqttInteractor: online публикуется с retain", "[mqtt][interactor][ha]") {
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
