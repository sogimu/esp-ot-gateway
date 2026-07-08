#pragma once

#include "application/ports/driven/imqtt_message_sink.h"

/// Fake-реализация IMqttMessageSink для хостовых тестов.
/// Однопоточный кольцевой буфер — не требует FreeRTOS.
class FakeMqttMessageSink : public IMqttMessageSink {
public:
    static constexpr int QUEUE_DEPTH = 8;

    bool push(const Message& msg) override {
        pushed_++;
        if (count_ >= QUEUE_DEPTH) {
            dropped_++;
            return false;
        }
        int i = (head_ + count_) % QUEUE_DEPTH;
        buf_[i] = msg;
        count_++;
        return true;
    }

    bool pop(Message& msg) override {
        if (count_ == 0) return false;
        popped_++;
        msg = buf_[head_];
        head_ = (head_ + 1) % QUEUE_DEPTH;
        count_--;
        return true;
    }

    /// Сбросить счётчики (для тестов)
    void reset_counts() {
        pushed_ = 0;
        popped_ = 0;
        dropped_ = 0;
    }

    /// Очистить очередь и счётчики
    void clear() {
        head_ = 0;
        count_ = 0;
        reset_counts();
    }

    // Публичные поля для проверок в тестах
    int pushed_  = 0;
    int popped_  = 0;
    int dropped_ = 0;
    int count_   = 0;  ///< Текущее количество элементов в очереди

private:
    Message buf_[QUEUE_DEPTH];
    int head_ = 0;
};
