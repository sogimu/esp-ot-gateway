#pragma once

#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "interfaces/isensors_observer.h"

class SensorsEndpoint {
public:
    SensorsEndpoint();
    ~SensorsEndpoint();

    void start();
    void stop();

    void subscribe(ISensorsObserver* obs);
    void unsubscribe(ISensorsObserver* obs);

private:
    static void task_wrapper(void* arg);
    void task_loop();

    TaskHandle_t task_;
    bool         running_;
    bool         converting_;
    int          skip_;

    std::vector<ISensorsObserver*> observers_;
};