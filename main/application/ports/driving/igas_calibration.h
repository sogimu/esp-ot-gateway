#pragma once

/// Gas meter calibration and correction commands.
class IGasCalibration {
public:
    virtual void set_k_calib(float) = 0;               // validated 0.1–10
    virtual void set_p_max(float) = 0;
    virtual void set_gas_calorific(float) = 0;
    virtual void set_gas_meter_base(float) = 0;
    virtual bool add_meter_correction(float reading) = 0;
    virtual void reset_corrections() = 0;

    // ── Boiler model config ──────────────────────────────────
    virtual void set_gas_temp_offset(float v) = 0;      // clamp [-20, +10]
    virtual void set_ch_power(float pmin, float pmax) = 0;
    virtual void set_dhw_power(float pmin, float pmax) = 0;
    virtual void set_efficiency_points(float t1, float v1, float t2, float v2, float t3, float v3) = 0;

    virtual ~IGasCalibration() = default;
};
