#pragma once

/// Gas meter calibration and correction commands.
class IGasCalibration {
public:
    virtual void set_k_calib(float) = 0;               // validated 0.1–10
    virtual void set_p_max(float) = 0;
    virtual void set_gas_calorific(float) = 0;
    virtual void set_gas_meter_base(float) = 0;
    virtual void add_meter_correction(float reading) = 0;
    virtual ~IGasCalibration() = default;
};
