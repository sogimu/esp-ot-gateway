#pragma once
#include <stdint.h>

/// Временные настройки NVS (tz_offset, SNTP-серверы).
/// Единственный оставшийся интерфейс после выделения всех портов (boiler/gas/predict/burn/heating).
/// Используется SystemConfigInteractor.
class ITimeSettingsStore {
public:
    virtual void load_time_settings(class IHeatingStateStore& state) = 0;
    virtual void save_time_settings(const class IHeatingStateStore& state) = 0;
    virtual ~ITimeSettingsStore() = default;
};
