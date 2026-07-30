#pragma once

#include "application/ports/driving/icontrol_task.h"

class ITemperatureSensor;
class IHeatingStateStore;

/// Polls DS18B20 temperature sensors.
/// Skips 4 out of 5 cycles (~5.5 s between reads).
class SensorsPollInteractor : public IControlTask {
public:
    SensorsPollInteractor(ITemperatureSensor& sensor, IHeatingStateStore& state);

    void execute() override;

private:
    ITemperatureSensor&  sensor_;
    IHeatingStateStore&  state_;
    int skip_ = 0;
};
