#pragma once
#include "application/ports/driven/itime_settings_store.h"

/// NVS-backed хранилище временных настроек (tz_offset, sntp_srv*).
/// Заменяет NvsConfigStore для namespace "config" — time-ключей.
class TimeSettingsNvsStore : public ITimeSettingsStore {
public:
    void init();   // nvs_flash_init + D10 erase-gate (PENDING_VERIFY)

    void load_time_settings(IHeatingStateStore& state) override;
    void save_time_settings(const IHeatingStateStore& state) override;
};
