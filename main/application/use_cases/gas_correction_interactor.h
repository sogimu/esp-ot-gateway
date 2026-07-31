#pragma once

#include "application/ports/driving/igas_calibration.h"
#include "application/ports/driven/igas_correction_store.h"
#include "nvs_config_store.h"  // NvsMeterBlob, NvsCorrLogEntry, CORRECTION_LOG_SIZE
#include "domain/services/kalman1d.h"

class IHeatingStateStore;
class ILogger;
class ITimeSource;
class GasFlowService;

/// Implements gas meter correction use case.
/// Owns the in-memory correction log (NvsMeterBlob) and persists it via IConfigurationStore.
class GasCorrectionInteractor : public IGasCalibration {
public:
    GasCorrectionInteractor(IHeatingStateStore& state, IGasCorrectionStore& store, ILogger& log,
                            ITimeSource* time = nullptr, GasFlowService* gas_flow = nullptr);

    void set_k_calib(float) override;
    void set_p_max(float) override;
    void set_gas_calorific(float) override;
    void set_gas_meter_base(float) override;
    bool add_meter_correction(float reading) override;
    void reset_corrections() override;

    // ── Boiler model config setters with validation ──────────
    void set_gas_temp_offset(float v) override;
    void set_ch_power(float pmin, float pmax) override;
    void set_dhw_power(float pmin, float pmax) override;
    void set_efficiency_points(float t1, float v1, float t2, float v2, float t3, float v3) override;

    /// Restore persisted correction log from NVS on boot.
    void init();

    /// Read-only access to the correction log for web rendering.
    const NvsMeterBlob& meter_blob() const { return meter_blob_; }

    void save_daily_gas(const void* blob) { store_.save_daily_gas(blob); }

private:
    IHeatingStateStore&   state_;
    IGasCorrectionStore&  store_;
    ILogger&             log_;
    ITimeSource*         time_ = nullptr;
    GasFlowService*      gas_flow_ = nullptr;
    NvsMeterBlob         meter_blob_;
    Kalman1D             kalman_k_{1.0f, 0.02f, 1.0f};
};
