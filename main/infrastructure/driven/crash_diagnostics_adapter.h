#pragma once

#include "application/ports/driven/idiagnostics.h"
#include "application/ports/driven/ilogger.h"

#include "esp_system.h"
#include "esp_core_dump.h"

/// IDiagnostics adapter — boot-time reset reason + coredump check.
class CrashDiagnosticsAdapter : public IDiagnostics {
public:
    explicit CrashDiagnosticsAdapter(ILogger& log);

    void check_on_boot(ILogger& log) override;

    void start();
    void stop();

    /// Returns true if the previous boot ended with a crash (core dump found).
    bool last_boot_had_crash() const { return last_boot_had_crash_; }

private:
    const char* reset_reason_str(esp_reset_reason_t reason);

    ILogger& log_;
    bool started_ = false;
    bool last_boot_had_crash_ = false;
};
