#pragma once

#include "application/ports/driven/igas_correction_store.h"

class IBoilerConfigStore;

/// NVS-backed хранилище газовой коррекции (журнал сверки, integral_m3) +
/// делегация сохранения котловой конфигурации в IBoilerConfigStore.
///
/// "meter" namespace — журнал сверки счётчика (NvsMeterBlob).
/// "stats"  namespace — integral_m3.
/// Котловая калибровка — делегируется через переданную ссылку на BoilerNvsStore.
class GasCorrectionNvsStore : public IGasCorrectionStore {
public:
    GasCorrectionNvsStore() = default;
    void init(IBoilerConfigStore& boiler) { boiler_ = &boiler; }

    bool load_meter(IHeatingStateStore& state, void* blob = nullptr) override;
    void save_meter(const IHeatingStateStore& state, const void* blob = nullptr) override;
    void save_integral(float value) override;
    void save_boiler_config(const IHeatingStateStore& state) override;

private:
    IBoilerConfigStore* boiler_ = nullptr;
};
