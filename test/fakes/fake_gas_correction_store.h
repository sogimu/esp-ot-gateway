#pragma once
#include "application/ports/driven/igas_correction_store.h"
#include <cstring>

struct FakeGasCorrectionStore : IGasCorrectionStore {
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
    void save_meter(const IHeatingStateStore&, const void*) override {}
    void save_integral(float) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}

    void save_today_gas(const void* blob) override {
        if (!blob) return;
        std::memcpy(&today_blob_, blob, sizeof(GasTodayBlob));
        has_today_ = true;
    }

    bool load_today_gas(void* blob) override {
        if (!has_today_ || !blob) return false;
        std::memcpy(blob, &today_blob_, sizeof(GasTodayBlob));
        return true;
    }

    void save_history_gas(const void* blob) override {
        if (!blob) return;
        std::memcpy(&history_blob_, blob, sizeof(GasHistoryBlob));
        has_history_ = true;
    }

    bool load_history_gas(void* blob) override {
        if (!has_history_ || !blob) return false;
        std::memcpy(blob, &history_blob_, sizeof(GasHistoryBlob));
        return true;
    }

    GasTodayBlob   today_blob_{};
    GasHistoryBlob history_blob_{};
    bool has_today_ = false;
    bool has_history_ = false;
};
