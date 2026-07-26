#pragma once
#include <cstdint>

class OtaValidityAdapter;
class BurnCycleService;
class ModulationStatsService;
class GasFlowService;
class GasCorrectionInteractor;
class HeatingStatsNvsStore;
class HeatingStateAdapter;
class SntpTimeAdapter;

/// Инкапсулирует задачи Persistence-цикла: периодический сброс NVS (~2.5 мин).
/// Управляет счётчиком save_tick и заморозкой записи во время PENDING_VERIFY (D9).
class PersistenceLoopInteractor {
public:
    PersistenceLoopInteractor(OtaValidityAdapter& ota,
                               BurnCycleService& burn_cycle_service,
                               ModulationStatsService& mod_stats,
                               GasFlowService& gas_flow,
                               GasCorrectionInteractor& gas_corr,
                               HeatingStatsNvsStore& heating_stats,
                               HeatingStateAdapter& ca_state,
                               SntpTimeAdapter& ca_time,
                               uint32_t& total_uptime_base_sec);

    /// Вызвать на каждой итерации главного цикла (~15с).
    /// Сохраняет данные каждые 10 вызовов, с заморозкой NVS при PENDING_VERIFY.
    void tick();

private:
    OtaValidityAdapter&     ota_;
    BurnCycleService&       burn_cycle_service_;
    ModulationStatsService& mod_stats_;
    GasFlowService&         gas_flow_;
    GasCorrectionInteractor& gas_corr_;
    HeatingStatsNvsStore&   heating_stats_;
    HeatingStateAdapter&    ca_state_;
    SntpTimeAdapter&        ca_time_;
    uint32_t&               total_uptime_base_sec_;

    int save_tick_ = 0;
};
