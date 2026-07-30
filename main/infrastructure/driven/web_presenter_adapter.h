#pragma once

#include <stddef.h>
#include <stdint.h>

class IHeatingStateStore;
class ILogger;
class IEventLogReader;
class ModulationStatsService;
class BurnCycleService;
class GasFlowService;
class GasCorrectionInteractor;
class PidQualityAssessor;
class FopdtEstimator;

/// JSON rendering from CA state + services.
/// Writes into caller-provided buffer — no heap, thread-safe.
class WebPresenterAdapter {
public:
    /// Конструктор для production — все зависимости.
    WebPresenterAdapter(IHeatingStateStore& state, IEventLogReader& log_reader,
                        class ITimeSource& time,
                        ModulationStatsService& mod_stats, BurnCycleService& burn_cycles,
                        GasFlowService& gas_flow, GasCorrectionInteractor& gas_corr,
                        uint32_t total_uptime_base);

    /// Конструктор для тестов — дефолтные nullptr'ы.
    WebPresenterAdapter() = default;

    // Для тестов, где не нужны все зависимости
    void set_state(IHeatingStateStore* s) { state_ = s; }
    void set_logger(ILogger* l)           { logger_ = l; }
    void set_pid_quality(const PidQualityAssessor* q) { pid_quality_ = q; }
    void set_fopdt_estimator(const FopdtEstimator* e) { fopdt_ = e; }

    /// Render status JSON into buf, returns length (excluding null terminator).
    int render_status(char* buf, size_t size);

    /// Render log JSON into buf, returns length.
    int render_log(char* buf, size_t size);
    /// Return pre-rendered log JSON (no copy needed).
    /// Caller MUST call log_lock() before and log_unlock() after
    /// httpd_resp_sendstr() — protects static buffer from concurrent access.
    const char* log_json();
    void log_lock();
    void log_unlock();

    /// Render stats JSON into buf, returns length.
    int render_stats(char* buf, size_t size);

    /// Render schedule JSON into buf, returns length.
    int render_schedule(char* buf, size_t size);

    /// Render PID schedule JSON into buf, returns length.
    int render_pid_schedule(char* buf, size_t size);

    /// Render PID quality JSON into buf, returns length.
    int render_pid_quality(char* buf, size_t size);

private:
    IHeatingStateStore*       state_ = nullptr;
    ILogger*                  logger_ = nullptr;     // for event() logging (optional)
    IEventLogReader*          log_reader_ = nullptr; // for lock/unlock/to_json
    ModulationStatsService*   mod_stats_ = nullptr;
    BurnCycleService*         burn_cycles_ = nullptr;
    GasFlowService*           gas_flow_ = nullptr;
    GasCorrectionInteractor*  gas_corr_ = nullptr;
    const PidQualityAssessor* pid_quality_ = nullptr;
    const FopdtEstimator*     fopdt_ = nullptr;
    class ITimeSource*        time_ = nullptr;
    uint32_t                  total_uptime_base_ = 0;

    /// Compute monthly error percentage from the last two corrections.
    float compute_monthly_error_pct() const;
};
