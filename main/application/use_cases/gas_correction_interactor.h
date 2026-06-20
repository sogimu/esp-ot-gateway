#pragma once

#include "application/ports/driving/igas_calibration.h"
#include "nvs_config_adapter.h"  // NvsMeterBlob, NvsCorrLogEntry, CORRECTION_LOG_SIZE

class IHeatingStateStore;
class IConfigurationStore;
class ILogger;
class ITimeSource;
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
    void reset_corrections() override;

    // ── Boiler model config setters with validation ──────────
    void set_gas_temp_offset(float v) override;
    void set_ch_power(float pmin_warm, float pmax_warm, float pmin_hot, float pmax_hot) override;
    void set_dhw_power(float pmin, float pmax) override;
    void set_efficiency_points(float t1, float v1, float t2, float v2, float t3, float v3) override;

    /// Wire up the gas flow service (needed for integral_m3 in correction calc).
    void set_gas_flow(GasFlowService* gf) { gas_flow_ = gf; }

    /// Wire up time source (needed for correction timestamps).
    void set_time_source(ITimeSource* t) { time_ = t; }

    /// Restore persisted correction log from NVS on boot.
    void init();

    /// Read-only access to the correction log for web rendering.
    const NvsMeterBlob& meter_blob() const { return meter_blob_; }

private:
    IHeatingStateStore&  state_;
    IConfigurationStore& config_;
    ILogger&             log_;
    ITimeSource*         time_ = nullptr;
    GasFlowService*      gas_flow_ = nullptr;
    NvsMeterBlob         meter_blob_;
};
