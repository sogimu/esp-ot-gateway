#pragma once

#include <stdint.h>

/// OpenTherm hardware abstraction — raw frame send/receive.
/// Uses opaque pointer to avoid dependency on opentherm.h OT_Frame type.
class IOtHardware {
public:
    virtual bool init() = 0;

    /// Send a request frame and receive a response.
    /// request and response are pointers to OT_Frame (declared in opentherm.h).
    virtual bool send_and_receive(const void* request, void* response) = 0;

    virtual ~IOtHardware() = default;
};
