#pragma once

#include "application/ports/driven/iot_hardware.h"

/// Wraps opentherm.c OT_Transaction() behind IOtHardware port.
class OtHardwareAdapter : public IOtHardware {
public:
    bool init() override;  // OT_Init() — configures GPIO4/GPIO16, timer, ISR
    bool send_and_receive(const void* request, void* response) override;
};
