#include "infrastructure/driven/ot_hardware_adapter.h"
#include "c_legacy/opentherm.h"

extern "C" {
    void OT_Init(void); // from opentherm.c — configures GPIO4/GPIO16, timer, ISR, semaphore
}

bool OtHardwareAdapter::init()
{
    OT_Init();
    return true;
}

bool OtHardwareAdapter::send_and_receive(const void* request, void* response)
{
    return OT_Transaction(
        static_cast<const OT_Frame*>(request),
        static_cast<OT_Frame*>(response));
}
