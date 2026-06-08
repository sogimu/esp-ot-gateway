#pragma once

/// Boot-time diagnostics: reset reason and coredump check.
class IDiagnostics {
public:
    virtual void check_on_boot(class ILogger& log) = 0;
    virtual ~IDiagnostics() = default;
};
