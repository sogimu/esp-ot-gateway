#pragma once

class ISensorsObserver {
public:
    virtual ~ISensorsObserver() = default;
    virtual void on_sensor_data(int sensor_id, float temperature) = 0;
};
