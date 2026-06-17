#include <catch2/catch_test_macros.hpp>

#include "application/ports/driven/imqtt_hardware.h"
#include "application/ports/driven/imqtt_message_sink.h"

TEST_CASE("IMqttHardware: может быть унаследован", "[mqtt][ports][compile]") {
    struct TestHw : IMqttHardware {
        bool connect(const char*, const char*, const char*,
                     const char*, const char*, bool, int) override { return true; }
        void disconnect() override {}
        bool reconnect() override { return true; }
        bool is_connected() const override { return false; }
        int publish(const char*, const char*, int, QoS, bool) override { return 1; }
        int subscribe(const char*, QoS) override { return 1; }
        int unsubscribe(const char*) override { return 1; }
        void set_event_callback(EventCallback, void*) override {}
    };
    TestHw hw;
    REQUIRE_FALSE(hw.is_connected());
}

TEST_CASE("IMqttMessageSink: константы размера буфера", "[mqtt][ports][compile]") {
    REQUIRE(IMqttMessageSink::Message::TOPIC_MAX == 64);
    REQUIRE(IMqttMessageSink::Message::PAYLOAD_MAX == 512);
}

TEST_CASE("IMqttMessageSink: push/pop цикл", "[mqtt][ports][compile]") {
    struct TestSink : IMqttMessageSink {
        bool push(const Message&) override { return true; }
        bool pop(Message&) override { return false; }
    };
    TestSink sink;
    IMqttMessageSink::Message msg{};
    REQUIRE(sink.push(msg));
    REQUIRE_FALSE(sink.pop(msg));
}
