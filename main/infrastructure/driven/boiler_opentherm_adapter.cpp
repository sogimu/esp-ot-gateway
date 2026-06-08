#include "infrastructure/driven/boiler_opentherm_adapter.h"
#include "c_legacy/opentherm.h"
#include <cmath>

BoilerOpenThermAdapter::BoilerOpenThermAdapter(IOtHardware& ot_hw)
    : ot_hw_(ot_hw)
{
    // state_ initialized in-class
}

IBoilerHardware::ReadResult BoilerOpenThermAdapter::read(uint8_t data_id)
{
    OT_Frame req = {};
    OT_Frame resp = {};
    req.msg_type =OT_MSG_READ_DATA;
    req.data_id = data_id;

    bool ok = ot_hw_.send_and_receive(static_cast<const void*>(&req), static_cast<void*>(&resp));
    ReadResult r;
    r.success = ok;
    if (ok) {
        r.value_f88 = static_cast<float>(resp.data_value) / 256.0f;
    } else {
        r.value_f88 = NAN;
    }
    return r;
}

IBoilerHardware::ReadResult BoilerOpenThermAdapter::read_status(uint8_t master_flags, bool fault_reset)
{
    OT_Frame req = {};
    OT_Frame resp = {};
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id = 0;  // STATUS
    uint8_t lb = fault_reset ? 1 : 0;
    req.data_value = static_cast<uint16_t>((master_flags << 8) | lb);

    bool ok = ot_hw_.send_and_receive(static_cast<const void*>(&req), static_cast<void*>(&resp));
    ReadResult r;
    r.success = ok;
    if (ok) {
        r.value_f88 = static_cast<float>(resp.data_value) / 256.0f;
        connected_ = true;
    } else {
        r.value_f88 = NAN;
    }
    return r;
}

IBoilerHardware::WriteResult BoilerOpenThermAdapter::write(uint8_t data_id, uint16_t value)
{
    OT_Frame req = {};
    OT_Frame resp = {};
    req.msg_type =OT_MSG_WRITE_DATA;
    req.data_id = data_id;
    req.data_value = value;

    bool ok = ot_hw_.send_and_receive(static_cast<const void*>(&req), static_cast<void*>(&resp));
    WriteResult r;
    r.success = ok;
    return r;
}

IBoilerHardware::WriteResult BoilerOpenThermAdapter::write_status(uint8_t flags, bool fault_reset)
{
    OT_Frame req = {};
    OT_Frame resp = {};
    req.msg_type =OT_MSG_WRITE_DATA;
    req.data_id = 0; // Status
    req.data_value = (static_cast<uint16_t>(flags) << 8) | (fault_reset ? 0x01 : 0x00);

    bool ok = ot_hw_.send_and_receive(static_cast<const void*>(&req), static_cast<void*>(&resp));
    WriteResult r;
    r.success = ok;
    return r;
}

bool BoilerOpenThermAdapter::is_connected() const
{
    return state_.connected;
}
