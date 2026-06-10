#pragma once

#include "application/ports/driven/itemperature_sensor.h"

/// ITemperatureSensor adapter wrapping sensors.c OneWire/DS18B20 code.
class TemperatureSensorAdapter : public ITemperatureSensor {
public:
    void init();  // configure GPIOs for DS18B20
    void request_conversion() override;
    Reading read_sensor(int id) override;
};
