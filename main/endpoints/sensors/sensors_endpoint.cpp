#include "sensors_endpoint.h"
#include "sensors.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "sensors_ep";

SensorsEndpoint::SensorsEndpoint()
    : task_(nullptr), running_(false)
    , converting_(false), skip_(0)
{
}

SensorsEndpoint::~SensorsEndpoint()
{
    stop();
}

void SensorsEndpoint::start()
{
    if (running_) return;
    sensors_init();
    running_ = true;
    xTaskCreate(task_wrapper, "sensors", 2048, this, 4, &task_);
}

void SensorsEndpoint::stop()
{
    if (!running_) return;
    running_ = false;
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void SensorsEndpoint::subscribe(ISensorsObserver* obs)
{
    for (auto* o : observers_) {
        if (o == obs) return;
    }
    observers_.push_back(obs);
}

void SensorsEndpoint::unsubscribe(ISensorsObserver* obs)
{
    for (auto it = observers_.begin(); it != observers_.end(); ++it) {
        if (*it == obs) { observers_.erase(it); return; }
    }
}

void SensorsEndpoint::task_wrapper(void* arg)
{
    auto* self = static_cast<SensorsEndpoint*>(arg);
    self->task_loop();
}

void SensorsEndpoint::task_loop()
{
    ESP_LOGI(TAG, "Sensors poll task started");
    while (running_) {
        skip_++;
        if (skip_ < 5) {
            vTaskDelay(pdMS_TO_TICKS(1100));
            continue;
        }
        skip_ = 0;

        if (!converting_) {
            sensors_poll();
            converting_ = sensor1_temp > -100.0f || sensor2_temp > -100.0f;
        } else {
            sensors_poll();
            converting_ = false;

            for (auto* o : observers_) {
                extern float sensor1_temp, sensor2_temp;
                if (sensor1_temp > -100.0f)
                    o->on_sensor_data(0, sensor1_temp);
                if (sensor2_temp > -100.0f)
                    o->on_sensor_data(1, sensor2_temp);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1100));
    }
    vTaskDelete(nullptr);
}