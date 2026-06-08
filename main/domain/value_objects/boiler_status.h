#pragma once

/// Boiler operational status flags.
/// Invariant: flame and fault cannot both be true simultaneously.
class BoilerStatus {
public:
    BoilerStatus() = default;
    BoilerStatus(bool flame, bool fault, bool ch_active, bool dhw_active);

    bool is_flame_on()   const { return flame_; }
    bool has_fault()     const { return fault_; }
    bool is_ch_active()  const { return ch_active_; }
    bool is_dhw_active() const { return dhw_active_; }

    /// Returns true if invariants hold.
    bool is_valid() const;

private:
    bool flame_      = false;
    bool fault_      = false;
    bool ch_active_  = false;
    bool dhw_active_ = false;
};
