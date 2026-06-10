#include "infrastructure/driven/temperature_sensor_adapter.h"

extern "C" {
    void sensors_init();       // from c_legacy/sensors.c
    void sensors_poll();
    extern float sensor1_temp;
    extern float sensor2_temp;
}

void TemperatureSensorAdapter::init()
{
    sensors_init(); // configure GPIO15/GPIO26
}

void TemperatureSensorAdapter::request_conversion()
{
    // CA SensorsPollInteractor is the sole sensor driver.
    // sensors_poll() manages its own 2-phase state machine
    // (start conversion → wait → read scratchpad).
    sensors_poll();
}

ITemperatureSensor::Reading TemperatureSensorAdapter::read_sensor(int id)
{
    Reading r;
    r.id = id;
    r.valid = false;
    r.temperature = -127.0f;

    if (id == 0) {
        r.temperature = sensor1_temp;
        r.valid = (sensor1_temp > -100.0f && sensor1_temp < 85.0f);  // reject DS18B20 power-on default (85°C)
    } else if (id == 1) {
        r.temperature = sensor2_temp;
        r.valid = (sensor2_temp > -100.0f && sensor2_temp < 85.0f);  // reject DS18B20 power-on default (85°C)
    }
    return r;
}
