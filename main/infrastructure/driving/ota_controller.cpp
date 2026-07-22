#include "infrastructure/driving/ota_controller.h"

#include "infrastructure/driven/ota_validity_adapter.h"
#include "infrastructure/driven/esp_ota_adapter.h"
#include "infrastructure/driven/ota_version_index_adapter.h"
#include "infrastructure/driving/ota_interactor.h"

#include "infrastructure/driven/heating_stats_nvs_store.h"
#include "infrastructure/driven/heating_state_adapter.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/modulation_stats_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "infrastructure/driven/sntp_time_adapter.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static const char* TAG = "ota_ctrl";

// ───────────────────────────────────────────────────────────────────────────
// FirmwareOtaInteractor — реализует FreeRTOS-швы OtaInteractor.
// ───────────────────────────────────────────────────────────────────────────
namespace {

class FirmwareOtaInteractor : public OtaInteractor {
public:
    using OtaInteractor::OtaInteractor;

protected:
    bool launch_download_task() override {
        constexpr uint32_t STACK = 12 * 1024;
        constexpr UBaseType_t PRIO = 3;   // ниже задачи опроса котла (main_poll=4)
        return xTaskCreate(trampoline, "ota_dl", STACK, this, PRIO, nullptr) == pdPASS;
    }
    void reboot_into_new_slot() override {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

private:
    static void trampoline(void* arg) {
        static_cast<FirmwareOtaInteractor*>(arg)->run_download();
    }
};

// ───────────────────────────────────────────────────────────────────────────
// Контекст flush-статистики перед перезагрузкой в новый слот.
// ───────────────────────────────────────────────────────────────────────────
struct FlushCtx {
    OtaController::Deps deps;
};

void flush_stats_cb(void* ctxv)
{
    auto* c = static_cast<FlushCtx*>(ctxv);
    const auto& d = c->deps;
    ESP_LOGI(TAG, "сброс статистики в NVS перед перезагрузкой в новый слот");

    const uint32_t bs = d.burn_cycle_service->burner_seconds();
    d.burn_cycle_service->save_to_store();

    static NvsHistBlob hist_blob;
    memset(&hist_blob, 0, sizeof(hist_blob));
    d.mod_stats->fill_histogram(hist_blob);
    d.heating_stats->save_stats(*d.state, bs, d.gas_flow->integral_m3(),
                      &hist_blob, nullptr, nullptr, nullptr);
    d.heating_stats->save_total_uptime(*d.total_uptime_base_sec +
                             (uint32_t)(d.time->monotonic_us() / 1000000));
    d.heating_stats->save_meter(*d.state, &d.gas_corr->meter_blob());
}

}  // namespace

// ───────────────────────────────────────────────────────────────────────────
// OtaController::Members — pimpl
// ───────────────────────────────────────────────────────────────────────────
struct OtaController::Members {
    OtaValidityAdapter      validity;         // Phase 1 (is_pending_global)
    EspOtaAdapter           esp_ota;          // Phase 8
    OtaVersionIndexAdapter  ota_versions;     // Phase 8
    FirmwareOtaInteractor*  interactor = nullptr;
    FlushCtx                flush_ctx;
};

// ───────────────────────────────────────────────────────────────────────────
// OtaController
// ───────────────────────────────────────────────────────────────────────────

OtaController::OtaController()
    : m_(std::make_unique<Members>())
{
    ESP_LOGI(TAG, "загружена партиция в состоянии %s",
             m_->validity.is_pending()
                 ? "PENDING_VERIFY (требуется подтверждение в течение 90 с)"
                 : "VALID (подтверждения не требуется)");
}

OtaController::~OtaController()
{
    delete m_->interactor;
    m_->interactor = nullptr;
}

void OtaController::set_crash_flag(bool crash)
{
    m_->validity.set_crash_flag(crash);
}

bool OtaController::is_pending() const
{
    return m_->validity.is_pending();
}

IOtaManager* OtaController::start(const Deps& deps)
{
    // Сохраняем зависимости flush-статистики (deps копируются — объекты,
    // на которые они ссылаются, живут в app_main весь сеанс).
    m_->flush_ctx.deps = deps;

    m_->interactor = new FirmwareOtaInteractor(
        m_->validity, m_->esp_ota, m_->ota_versions,
        []() { return esp_timer_get_time() / 1000; });

    m_->interactor->set_flush_stats_callback(flush_stats_cb, &m_->flush_ctx);

    m_->validity.set_http_server_up(true);
    m_->validity.arm();

    ESP_LOGI(TAG, "OTA-подсистема запущена");
    return m_->interactor;
}

void OtaController::tick()
{
    m_->validity.heartbeat();
    if (m_->interactor != nullptr) {
        m_->interactor->poll();
    }
}
