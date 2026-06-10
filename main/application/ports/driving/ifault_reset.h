#pragma once

/// Fault reset command from web UI.
class IFaultReset {
public:
    virtual void reset() = 0;
    virtual ~IFaultReset() = default;
};
