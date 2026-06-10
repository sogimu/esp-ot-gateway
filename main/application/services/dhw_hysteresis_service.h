#pragma once

/// Pure function: DHW hysteresis evaluation.
/// Returns true if DHW should be heated (below setpoint - hysteresis).
class DHWHysteresisService {
public:
    /// @param dhw_temp      current DHW tank temperature, C
    /// @param dhw_setpoint  target temperature, C
    /// @param hysteresis    hysteresis band, C (e.g. 2.0)
    /// @param dhw_enabled   whether DHW mode is enabled
    /// @return true if DHW heating should be active
    static bool should_heat(float dhw_temp, float dhw_setpoint, float hysteresis, bool dhw_enabled);

    /// @return true if DHW heating should stop (temp reached setpoint)
    static bool should_stop(float dhw_temp, float dhw_setpoint);
};
