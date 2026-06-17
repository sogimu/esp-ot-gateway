#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include "fakes/fake_mqtt_message_sink.h"

TEST_CASE("FakeMqttMessageSink: push/pop в порядке FIFO", "[mqtt][queue]") {
    FakeMqttMessageSink q;
    IMqttMessageSink::Message m1{"topic/a", "data1", 5};
    IMqttMessageSink::Message m2{"topic/b", "data2", 5};

    REQUIRE(q.push(m1));
    REQUIRE(q.push(m2));
    REQUIRE(q.pushed_ == 2);

    IMqttMessageSink::Message out;
    REQUIRE(q.pop(out));
    REQUIRE(std::string(out.topic) == "topic/a");
    REQUIRE(q.popped_ == 1);

    REQUIRE(q.pop(out));
    REQUIRE(std::string(out.topic) == "topic/b");
    REQUIRE(q.popped_ == 2);

    // Очередь пуста
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("FakeMqttMessageSink: переполнение", "[mqtt][queue]") {
    FakeMqttMessageSink q;
    IMqttMessageSink::Message m;

    // Заполняем очередь (глубина 8)
    for (int i = 0; i < 8; i++) {
        snprintf(m.topic, sizeof(m.topic), "topic/%d", i);
        REQUIRE(q.push(m));
    }
    REQUIRE(q.pushed_ == 8);
    REQUIRE(q.dropped_ == 0);

    // 9-е сообщение должно быть отброшено
    snprintf(m.topic, sizeof(m.topic), "topic/overflow");
    REQUIRE_FALSE(q.push(m));
    REQUIRE(q.dropped_ == 1);
    REQUIRE(q.pushed_ == 9);
}

TEST_CASE("FakeMqttMessageSink: кольцевое поведение", "[mqtt][queue]") {
    FakeMqttMessageSink q;

    // Push 5, pop 3, push 4 — проверка кольцевого буфера
    IMqttMessageSink::Message m;
    for (int i = 0; i < 5; i++) {
        snprintf(m.topic, sizeof(m.topic), "t%d", i);
        q.push(m);
    }
    IMqttMessageSink::Message out;
    for (int i = 0; i < 3; i++) q.pop(out);

    for (int i = 5; i < 9; i++) {
        snprintf(m.topic, sizeof(m.topic), "t%d", i);
        q.push(m);
    }

    // Первое после трёх pop'ов должно быть "t3"
    REQUIRE(q.pop(out));
    REQUIRE(std::string(out.topic) == "t3");
}

TEST_CASE("FakeMqttMessageSink: reset_counts", "[mqtt][queue]") {
    FakeMqttMessageSink q;
    IMqttMessageSink::Message m;
    q.push(m);
    q.pop(m);
    REQUIRE(q.pushed_ == 1);
    REQUIRE(q.popped_ == 1);

    q.reset_counts();
    REQUIRE(q.pushed_ == 0);
    REQUIRE(q.popped_ == 0);
}

TEST_CASE("FakeMqttMessageSink: clear удаляет все элементы", "[mqtt][queue]") {
    FakeMqttMessageSink q;
    IMqttMessageSink::Message m;
    for (int i = 0; i < 3; i++) {
        snprintf(m.topic, sizeof(m.topic), "t%d", i);
        q.push(m);
    }
    REQUIRE(q.count_ == 3);

    q.clear();
    REQUIRE(q.count_ == 0);
    REQUIRE_FALSE(q.pop(m));
}
