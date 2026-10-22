#pragma once

/// Single abstraction for reading corrected (offset-adjusted) temperatures.
///
/// All application logic MUST read temperatures through this port, never
/// through the raw temperature getters on IHeatingStateStore. The raw getters
/// remain available only to the writers (sensors/boiler poll) and the adapter.
class ITemperatureSource {
public:
    virtual float t1() = 0;          ///< DS18B20 T1, corrected
    virtual float t2() = 0;          ///< DS18B20 T2, corrected
    virtual float outside() = 0;     ///< boiler outside temperature, corrected
    virtual float ch_flow() = 0;     ///< boiler CH supply temperature, corrected
    virtual float ch_return() = 0;   ///< boiler CH return temperature, corrected
    virtual float dhw() = 0;         ///< boiler DHW temperature, corrected

    virtual ~ITemperatureSource() = default;
};
