#pragma once

#include "application/ports/driving/igas_calibration.h"
#include "nvs_config_adapter.h"  // NvsMeterBlob, NvsCorrLogEntry, CORRECTION_LOG_SIZE

class IHeatingStateStore;
class IConfigurationStore;
class ILogger;
class GasFlowService;

/// Implements gas meter correction use case.
/// Owns the in-memory correction log (NvsMeterBlob) and persists it via IConfigurationStore.
class GasCorrectionInteractor : public IGasCalibration {
public:
    GasCorrectionInteractor(IHeatingStateStore& state, IConfigurationStore& config, ILogger& log);

    void set_k_calib(float) override;
    void set_p_max(float) override;
    void set_gas_calorific(float) override;
    void set_gas_meter_base(float) override;
    void add_meter_correction(float reading) override;

    /// Wire up the gas flow service (needed for integral_m3 in correction calc).
    void set_gas_flow(GasFlowService* gf) { gas_flow_ = gf; }

    /// Restore persisted correction log from NVS on boot.
    void init();

    /// Read-only access to the correction log for web rendering.
    const NvsMeterBlob& meter_blob() const { return meter_blob_; }

private:
    IHeatingStateStore&  state_;
    IConfigurationStore& config_;
    ILogger&             log_;
    GasFlowService*      gas_flow_ = nullptr;
    NvsMeterBlob         meter_blob_;
};
