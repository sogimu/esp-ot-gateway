#include "application/use_cases/supervision_loop_interactor.h"

#include "infrastructure/driven/ota_validity_adapter.h"
#include "infrastructure/driven/event_log_adapter.h"
#include "infrastructure/driving/http_controller_adapter.h"
#include "infrastructure/driving/wifi_apsta_adapter.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

SupervisionLoopInteractor::SupervisionLoopInteractor(OtaValidityAdapter& ota,
                                                       EventLogAdapter& log,
                                                       HttpControllerAdapter& http,
                                                       WifiApStaAdapter& wifi)
    : ota_(ota), log_(log), http_(http), wifi_(wifi) {}

void SupervisionLoopInteractor::tick(uint32_t free_heap, uint32_t largest_free)
{
    ota_.heartbeat();
    wifi_.try_recover_ap();

    // ── Recovery ladder ────────────────────────────────────
    static int recovery_level = 0;
    if (largest_free < 4096 || free_heap < 8192) {
        ESP_LOGE("main", "Куча исчерпана (своб=%" PRIu32 " крупн=%" PRIu32 ") — перезагрузка",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Перезагрузка: куча исчерпана (%" PRIu32 "/%" PRIu32 ")",
                   free_heap, largest_free);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else if (largest_free < 6144 && recovery_level < 3) {
        recovery_level = 3;
        ESP_LOGW("main", "Recovery L3: перезапуск HTTP (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L3: перезапуск HTTP");
        http_.stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        http_.start();
    } else if (largest_free < 12288 && recovery_level < 2) {
        recovery_level = 2;
        ESP_LOGW("main", "Recovery L2: фрагментация (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L2: фрагментация кучи");
    } else if (largest_free < 16384 && recovery_level < 1) {
        recovery_level = 1;
        ESP_LOGW("main", "Recovery L1: предупреждение (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L1: фрагментация растёт");
    }
    if (largest_free >= 32768) recovery_level = 0;
}
