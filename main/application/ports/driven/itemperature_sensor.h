#pragma once

/// Temperature sensor port — abstracts DS18B20 OneWire.
class ITemperatureSensor {
public:
    struct Reading { int id; float temperature; bool valid; };

    /// Start conversion on all sensors (non-blocking).
    virtual void request_conversion() = 0;

    /// Read a specific sensor by id (0=T1, 1=T2).
    /// Returns valid=false on CRC error or timeout.
    virtual Reading read_sensor(int id) = 0;

    virtual ~ITemperatureSensor() = default;
};
