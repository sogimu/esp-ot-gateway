#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include "fakes/fake_mqtt_hardware.h"

TEST_CASE("FakeMqttHardware: connect/disconnect", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    REQUIRE_FALSE(mqtt.is_connected());
    mqtt.connect("mqtt://localhost", nullptr, nullptr, nullptr, nullptr, true, 60);
    REQUIRE(mqtt.is_connected());
    mqtt.disconnect();
    REQUIRE_FALSE(mqtt.is_connected());
}

TEST_CASE("FakeMqttHardware: publish запоминает сообщения", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    mqtt.publish("test/topic", "hello", 5, IMqttHardware::QoS::AT_LEAST_ONCE, true);
    REQUIRE(mqtt.publishes_.size() == 1);
    REQUIRE(std::string(mqtt.publishes_[0].topic) == "test/topic");
    REQUIRE(mqtt.publishes_[0].retain == true);
    REQUIRE(mqtt.publishes_[0].qos == IMqttHardware::QoS::AT_LEAST_ONCE);
}

TEST_CASE("FakeMqttHardware: publish без retain", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    mqtt.publish("status/topic", "data", -1, IMqttHardware::QoS::AT_MOST_ONCE, false);
    REQUIRE(mqtt.publishes_.size() == 1);
    REQUIRE_FALSE(mqtt.publishes_[0].retain);
    REQUIRE(std::string(mqtt.publishes_[0].data) == "data");
}

TEST_CASE("FakeMqttHardware: инжекция connected вызывает колбек", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    int event_received = -1;
    mqtt.set_event_callback([](int event_id, void*, void* ctx) {
        *(int*)ctx = event_id;
    }, &event_received);

    mqtt.inject_connected();
    REQUIRE(mqtt.is_connected());
    REQUIRE(event_received == 0);
}

TEST_CASE("FakeMqttHardware: инжекция disconnected вызывает колбек", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    int event_received = -1;
    // Stateless lambda (нет захвата) конвертируется в C-указатель
    mqtt.set_event_callback([](int event_id, void*, void* ctx) {
        *(int*)ctx = event_id;
    }, &event_received);

    mqtt.inject_connected();
    mqtt.inject_disconnected();
    REQUIRE_FALSE(mqtt.is_connected());
    REQUIRE(event_received == 1);
}

TEST_CASE("FakeMqttHardware: инжекция сообщения вызывает колбек с данными", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    int events_fired = 0;

    mqtt.set_event_callback([](int event_id, void*, void* ctx) {
        if (event_id == 2) (*(int*)ctx)++;
    }, &events_fired);

    mqtt.inject_message("cmd/control", "{\"ch_enable\":0}", 15);
    REQUIRE(events_fired == 1);
}

TEST_CASE("FakeMqttHardware: last_publish_to находит сообщение", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    mqtt.publish("homeassistant/sensor/esp01/ch_temp/config", "{}", 2,
                 IMqttHardware::QoS::AT_LEAST_ONCE, true);
    mqtt.publish("esp-ot-gateway/status", "{\"t\":1}", -1,
                 IMqttHardware::QoS::AT_LEAST_ONCE, false);

    auto* p = mqtt.last_publish_to("status");
    REQUIRE(p != nullptr);
    REQUIRE_FALSE(p->retain);
    REQUIRE(std::string(p->data) == "{\"t\":1}");
}

TEST_CASE("FakeMqttHardware: count_publishes_to считает сообщения", "[mqtt][client][fake]") {
    FakeMqttHardware mqtt;
    mqtt.publish("homeassistant/sensor/a/config", "{}", 2, IMqttHardware::QoS::AT_LEAST_ONCE, true);
    mqtt.publish("homeassistant/sensor/b/config", "{}", 2, IMqttHardware::QoS::AT_LEAST_ONCE, true);
    mqtt.publish("other/topic", "x", 1, IMqttHardware::QoS::AT_MOST_ONCE, false);

    REQUIRE(mqtt.count_publishes_to("homeassistant") == 2);
    REQUIRE(mqtt.count_publishes_to("other") == 1);
    REQUIRE(mqtt.count_publishes_to("nonexistent") == 0);
}
