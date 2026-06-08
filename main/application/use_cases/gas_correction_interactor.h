#pragma once

#include "application/ports/driving/igas_calibration.h"

class IHeatingStateStore;
class IConfigurationStore;
class ILogger;

/// Implements gas meter correction use case.
class GasCorrectionInteractor : public IGasCalibration {
public:
    GasCorrectionInteractor(IHeatingStateStore& state, IConfigurationStore& config, ILogger& log);

    void set_k_calib(float) override;
    void set_p_max(float) override;
    void set_gas_calorific(float) override;
    void set_gas_meter_base(float) override;
    void add_meter_correction(float reading) override;

private:
    IHeatingStateStore&  state_;
    IConfigurationStore& config_;
    ILogger&             log_;
};
