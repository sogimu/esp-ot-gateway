#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class IMainPoller;

/// FreeRTOS task adapter — calls IMainPoller::run_once() every ~1.1s.
class MainPollerTaskAdapter {
public:
    explicit MainPollerTaskAdapter(IMainPoller& poller);

    void start();
    void stop();

private:
    IMainPoller& poller_;
    TaskHandle_t task_ = nullptr;
    static void task_loop(void* arg);

    static constexpr int STACK_SIZE = 8192;  // 6 IPollable + OT_Transaction + sensors_poll
    static constexpr int PRIORITY = 5;
    static constexpr int INTERVAL_MS = 1100;
};
