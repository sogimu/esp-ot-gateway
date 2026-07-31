#pragma once
#include "application/ports/driven/igas_correction_store.h"
#include <cstring>

struct FakeGasCorrectionStore : IGasCorrectionStore {
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
    void save_meter(const IHeatingStateStore&, const void*) override {}
    void save_integral(float) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}

    void save_daily_gas(const void* blob) override {
        if (!blob) return;
        std::memcpy(&daily_blob_, blob, sizeof(GasDailyBlob));
        has_daily_ = true;
    }

    bool load_daily_gas(void* blob) override {
        if (!has_daily_ || !blob) return false;
        std::memcpy(blob, &daily_blob_, sizeof(GasDailyBlob));
        return true;
    }

    GasDailyBlob daily_blob_{};
    bool has_daily_ = false;
};
