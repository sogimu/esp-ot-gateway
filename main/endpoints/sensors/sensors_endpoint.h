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
    float        prev_t1_;
    float        prev_t2_;

    std::vector<ISensorsObserver*> observers_;
};