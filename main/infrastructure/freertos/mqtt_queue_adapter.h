#pragma once

#include "application/ports/driven/imqtt_message_sink.h"
#include "infrastructure/freertos/mqtt_queue.h"
#include <cstdio>
#include <cstring>

/// Реализация IMqttMessageSink через FreeRTOS очередь.
/// Потокобезопасна: push() и pop() можно вызывать из разных задач FreeRTOS.
class FreeRtosMqttQueue : public IMqttMessageSink {
public:
    FreeRtosMqttQueue()
        : queue_(mqtt_queue_create())
    {}

    ~FreeRtosMqttQueue() override {
        if (queue_) mqtt_queue_delete(queue_);
    }

    bool push(const Message& msg) override {
        if (!queue_) return false;
        MqttQueueItem item;
        snprintf(item.topic, sizeof(item.topic), "%.63s", msg.topic);
        int copy_len = msg.payload_len;
        if (copy_len < 0) copy_len = 0;
        if (copy_len >= (int)sizeof(item.payload)) copy_len = (int)sizeof(item.payload) - 1;
        memcpy(item.payload, msg.payload, (size_t)copy_len);
        item.payload[copy_len] = '\0';
        item.payload_len = copy_len;
        return mqtt_queue_push(queue_, &item);
    }

    bool pop(Message& msg) override {
        if (!queue_) return false;
        MqttQueueItem item;
        if (!mqtt_queue_pop(queue_, &item)) return false;
        snprintf(msg.topic, sizeof(msg.topic), "%.63s", item.topic);
        int copy_len = item.payload_len;
        if (copy_len < 0) copy_len = 0;
        if (copy_len >= (int)sizeof(msg.payload)) copy_len = (int)sizeof(msg.payload) - 1;
        memcpy(msg.payload, item.payload, (size_t)copy_len);
        msg.payload[copy_len] = '\0';
        msg.payload_len = copy_len;
        return true;
    }

private:
    MqttQueueHandle queue_;
};
