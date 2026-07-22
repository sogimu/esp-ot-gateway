#pragma once

#include <cstdint>
#include <memory>

#include "application/ports/driving/iota_manager.h"  // IOtaManager

class NvsConfigStore;
class HeatingStateAdapter;
class BurnCycleService;
class ModulationStatsService;
class GasFlowService;
class GasCorrectionInteractor;
class SntpTimeAdapter;

/// Driving adapter: владеет всем OTA-подсистемой (адаптеры валидности,
/// загрузки, каталога версий и интерактор). Конструируется в Phase 1
/// (сразу после nvs.init()), start() вызывается в Phase 8.
///
/// Все конкретные OTA-типы и FreeRTOS-зависимости скрыты в .cpp —
/// main.cpp видит только этот заголовок.
class OtaController {
public:
    struct Deps {
        NvsConfigStore*          nvs;
        class BurnStatsNvsStore* burn_stats;
        HeatingStateAdapter*     state;
        BurnCycleService*        burn_cycle_service;
        ModulationStatsService*  mod_stats;
        GasFlowService*          gas_flow;
        GasCorrectionInteractor* gas_corr;
        SntpTimeAdapter*         time;
        uint32_t*                total_uptime_base_sec;
    };

    OtaController();
    ~OtaController();

    // ── Phase 1 (после nvs.init()) ────────────────────────
    void set_crash_flag(bool crash);

    // ── Phase 8 (после http.start()) ──────────────────────
    /// Создать FirmwareOtaInteractor, flush-stats, взвести валидацию.
    /// Deps задаются здесь — все зависимости готовы к этому моменту.
    IOtaManager* start(const Deps& deps);

    // ── Главный цикл ──────────────────────────────────────
    void tick();              ///< heartbeat + poll (каждые 15 с)
    bool is_pending() const;  ///< для NVS-заморозки D9

private:
    struct Members;  // pimpl — определён в .cpp
    std::unique_ptr<Members> m_;
};
