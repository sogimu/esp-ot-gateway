#pragma once

#include <stddef.h>
#include <stdint.h>

class IHeatingStateStore;
class ILogger;
class ModulationStatsService;
class BurnCycleService;
class GasFlowService;
class GasCorrectionInteractor;

/// JSON rendering from CA state + services.
/// Writes into caller-provided buffer — no heap, thread-safe.
class WebPresenterAdapter {
public:
    void set_state(IHeatingStateStore* s)    { state_ = s; }
    void set_logger(ILogger* l)              { logger_ = l; }
    void set_mod_stats(ModulationStatsService* m) { mod_stats_ = m; }
    void set_burn_cycles(BurnCycleService* b)    { burn_cycles_ = b; }
    void set_gas_flow(GasFlowService* g)         { gas_flow_ = g; }
    void set_gas_correction(GasCorrectionInteractor* g) { gas_corr_ = g; }
    void set_total_uptime_base(uint32_t sec)     { total_uptime_base_ = sec; }
    void set_time_source(class ITimeSource* t)   { time_ = t; }

    /// Render status JSON into buf, returns length (excluding null terminator).
    int render_status(char* buf, size_t size);

    /// Render log JSON into buf, returns length.
    int render_log(char* buf, size_t size);
    /// Return pre-rendered log JSON (no copy needed).
    const char* log_json();

    /// Render stats JSON into buf, returns length.
    int render_stats(char* buf, size_t size);

    /// Render schedule JSON into buf, returns length.
    int render_schedule(char* buf, size_t size);

    /// Render PID schedule JSON into buf, returns length.
    int render_pid_schedule(char* buf, size_t size);

private:
    IHeatingStateStore*       state_ = nullptr;
    ILogger*                  logger_ = nullptr;
    ModulationStatsService*   mod_stats_ = nullptr;
    BurnCycleService*         burn_cycles_ = nullptr;
    GasFlowService*           gas_flow_ = nullptr;
    GasCorrectionInteractor*  gas_corr_ = nullptr;
    class ITimeSource*        time_ = nullptr;
    uint32_t                  total_uptime_base_ = 0;
};
