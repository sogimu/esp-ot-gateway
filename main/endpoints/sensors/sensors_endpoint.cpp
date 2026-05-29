#include "sensors_endpoint.h"
#include "sensors.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "sensors_ep";

static const float SENSOR_INVALID = -127.0f;
static const float SENSOR_VALID_THRESHOLD = -100.0f;

SensorsEndpoint::SensorsEndpoint()
    : task_(nullptr), running_(false)
    , prev_t1_(SENSOR_INVALID), prev_t2_(SENSOR_INVALID)
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
        sensors_poll();

        if (sensor1_temp > SENSOR_VALID_THRESHOLD && sensor1_temp != prev_t1_) {
            prev_t1_ = sensor1_temp;
            for (auto* o : observers_)
                o->on_sensor_data(0, sensor1_temp);
        }
        if (sensor2_temp > SENSOR_VALID_THRESHOLD && sensor2_temp != prev_t2_) {
            prev_t2_ = sensor2_temp;
            for (auto* o : observers_)
                o->on_sensor_data(1, sensor2_temp);
        }

        vTaskDelay(pdMS_TO_TICKS(1100));
    }
    vTaskDelete(nullptr);
}