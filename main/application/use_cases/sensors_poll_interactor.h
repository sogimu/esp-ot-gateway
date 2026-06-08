#pragma once

#include "application/ports/driving/ipollable.h"

class ITemperatureSensor;
class IHeatingStateStore;

/// Polls DS18B20 temperature sensors.
/// Skips 4 out of 5 cycles (~5.5 s between reads).
class SensorsPollInteractor : public IPollable {
public:
    SensorsPollInteractor(ITemperatureSensor& sensor, IHeatingStateStore& state);

    void poll() override;

private:
    ITemperatureSensor&  sensor_;
    IHeatingStateStore&  state_;
    int skip_ = 0;
};
