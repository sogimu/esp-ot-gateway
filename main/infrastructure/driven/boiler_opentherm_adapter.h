#pragma once

#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/iot_hardware.h"
#include "c_legacy/opentherm.h"  // for OT_State, OT_Frame

/// IBoilerHardware adapter wrapping OpenTherm protocol via IOtHardware.
class BoilerOpenThermAdapter : public IBoilerHardware {
public:
    BoilerOpenThermAdapter(IOtHardware& ot_hw);

    ReadResult  read(uint8_t data_id) override;
    ReadResult  read_status(uint8_t master_flags, bool fault_reset) override;
    WriteResult write(uint8_t data_id, uint16_t value) override;
    WriteResult write_status(uint8_t master_flags, bool fault_reset) override;
    bool        is_connected() const override;

    OT_State*   ot_state() { return &state_; }

private:
    IOtHardware& ot_hw_;
    OT_State     state_{};
    bool         connected_ = false;
};
