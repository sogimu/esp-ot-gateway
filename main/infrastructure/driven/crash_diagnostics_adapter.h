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

private:
    const char* reset_reason_str(esp_reset_reason_t reason);

    ILogger& log_;
    bool started_ = false;
};
