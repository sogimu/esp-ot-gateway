#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class IControlLoop;

/// FreeRTOS task adapter — calls IControlLoop::run_once() every ~1.1s.
class ControlLoopTaskAdapter {
public:
    explicit ControlLoopTaskAdapter(IControlLoop& poller);

    void start();
    void stop();

private:
    IControlLoop& poller_;
    TaskHandle_t task_ = nullptr;
    static void task_loop(void* arg);

    static constexpr int STACK_SIZE = 8192;  // 6 IControlTask + OT_Transaction + sensors_poll
    static constexpr int PRIORITY = 4;  // ниже MQTT(5) — чтобы PING_RESP не терялся
    static constexpr int INTERVAL_MS = 1100;
};
