#pragma once

#include <stdint.h>
#include "domain/value_objects/boiler_status.h"
#include "domain/value_objects/temperature.h"
#include "domain/value_objects/modulation.h"

/// Aggregate root for the heating system.
/// Holds boiler state with invariants.
class HeatingSystem {
public:
    HeatingSystem() = default;

    // ── Mutators ────────────────────────────────────
    void apply_status(const BoilerStatus& status);
    void set_supply_temp(const Temperature& t);
    void set_return_temp(const Temperature& t);
    void set_dhw_temp(const Temperature& t);
    void set_outside_temp(const Temperature& t);
    void set_modulation(const Modulation& m);
    void set_fault_codes(uint8_t asf, uint8_t oem, uint16_t diag);
    void set_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs);
    void set_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh);

    // ── Accessors ───────────────────────────────────
    BoilerStatus status() const { return status_; }
    Temperature  supply_temp()   const { return supply_temp_; }
    Temperature  return_temp()   const { return return_temp_; }
    Temperature  dhw_temp()      const { return dhw_temp_; }
    Temperature  outside_temp()  const { return outside_temp_; }
    Modulation   modulation()    const { return modulation_; }

    uint8_t  asf_flags()        const { return asf_flags_; }
    uint8_t  oem_fault_code()   const { return oem_fault_code_; }
    uint16_t oem_diagnostic()   const { return oem_diagnostic_; }

    uint16_t burner_starts()    const { return burner_starts_; }
    uint16_t ch_pump_starts()   const { return ch_pump_starts_; }
    uint16_t dhw_valve_starts() const { return dhw_valve_starts_; }
    uint16_t dhw_burner_starts() const { return dhw_burner_starts_; }
    uint16_t burner_hours()     const { return burner_hours_; }
    uint16_t ch_pump_hours()    const { return ch_pump_hours_; }
    uint16_t dhw_valve_hours()  const { return dhw_valve_hours_; }
    uint16_t dhw_burner_hours() const { return dhw_burner_hours_; }

    /// Returns false if any invariant is violated.
    bool is_valid() const;

private:
    BoilerStatus  status_;
    Temperature   supply_temp_{0, Temperature::Range::CH};
    Temperature   return_temp_{0, Temperature::Range::CH};
    Temperature   dhw_temp_{0, Temperature::Range::DHW};
    Temperature   outside_temp_{0, Temperature::Range::AMBIENT};
    Modulation    modulation_{0};

    uint8_t       asf_flags_ = 0;
    uint8_t       oem_fault_code_ = 0;
    uint16_t      oem_diagnostic_ = 0;

    uint16_t      burner_starts_ = 0;
    uint16_t      ch_pump_starts_ = 0;
    uint16_t      dhw_valve_starts_ = 0;
    uint16_t      dhw_burner_starts_ = 0;

    uint16_t      burner_hours_ = 0;
    uint16_t      ch_pump_hours_ = 0;
    uint16_t      dhw_valve_hours_ = 0;
    uint16_t      dhw_burner_hours_ = 0;
};
