#include "application/use_cases/persistence_loop_interactor.h"

#include "infrastructure/driven/ota_validity_adapter.h"
#include "infrastructure/driven/nvs_config_store.h"  // NvsHistBlob
#include "infrastructure/driven/heating_stats_nvs_store.h"
#include "infrastructure/driven/heating_state_adapter.h"
#include "application/ports/driven/igas_correction_store.h"  // GasDailyBlob

#include "application/services/burn_cycle_service.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "infrastructure/driven/sntp_time_adapter.h"

#include <cstring>

#include "esp_log.h"

PersistenceLoopInteractor::PersistenceLoopInteractor(
        OtaValidityAdapter& ota,
        BurnCycleService& burn_cycle_service,
        ModulationStatsService& mod_stats,
        GasFlowService& gas_flow,
        GasCorrectionInteractor& gas_corr,
        HeatingStatsNvsStore& heating_stats,
        HeatingStateAdapter& ca_state,
        SntpTimeAdapter& ca_time,
        uint32_t& total_uptime_base_sec)
    : ota_(ota), burn_cycle_service_(burn_cycle_service), mod_stats_(mod_stats),
      gas_flow_(gas_flow), gas_corr_(gas_corr), heating_stats_(heating_stats),
      ca_state_(ca_state), ca_time_(ca_time),
      total_uptime_base_sec_(total_uptime_base_sec) {}

void PersistenceLoopInteractor::tick()
{
    save_tick_++;
    if (save_tick_ < 10) return;
    if (ota_.is_pending()) {
        ESP_LOGW("main", "Периодический NVS-save пропущен: образ на проверке (PENDING_VERIFY)");
        return;
    }
    if (OtaValidityAdapter::is_flushing_global()) {
        ESP_LOGD("main", "Периодический NVS-save пропущен: идёт OTA-flush");
        return;
    }
    save_tick_ = 0;

    uint32_t bs = burn_cycle_service_.burner_seconds();
    burn_cycle_service_.save_to_store();
    NvsHistBlob hist_blob; memset(&hist_blob, 0, sizeof(hist_blob));
    mod_stats_.fill_histogram(hist_blob);
    heating_stats_.save_stats(ca_state_, bs, gas_flow_.integral_m3(),
                              &hist_blob, nullptr, nullptr, nullptr);
    heating_stats_.save_total_uptime(total_uptime_base_sec_ +
        (uint32_t)(ca_time_.monotonic_us() / 1000000));
    heating_stats_.save_meter(ca_state_, &gas_corr_.meter_blob());

    GasDailyBlob daily_blob;
    gas_flow_.pack_daily(daily_blob);
    gas_corr_.save_daily_gas(&daily_blob);
}
