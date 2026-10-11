#pragma once

/// Pure function: CH (central heating) hysteresis evaluation.
/// Returns true if CH should be heated (flow below setpoint - hysteresis).
///
/// Note: hysteresis=0 is treated here as a zero-width band (heat while flow < setpoint).
/// The "hysteresis disabled → always heat" semantics are implemented by the caller
/// (BoilerPollInteractor), which short-circuits when hysteresis <= 0.
class CHHysteresisService {
public:
    /// @param ch_flow       current CH supply/flow temperature, C
    /// @param ch_setpoint   target CH flow temperature, C
    /// @param hysteresis    hysteresis band, C
    /// @param ch_enabled    whether CH mode is enabled
    /// @return true if CH heating should be active
    static bool should_heat(float ch_flow, float ch_setpoint, float hysteresis, bool ch_enabled);

    /// @return true if CH heating should stop (flow reached setpoint)
    static bool should_stop(float ch_flow, float ch_setpoint);
};
