#include "application/use_cases/supervision_loop_interactor.h"

#include "infrastructure/driven/ota_validity_adapter.h"
#include "infrastructure/driven/event_log_adapter.h"
#include "infrastructure/driving/http_controller_adapter.h"
#include "infrastructure/driving/wifi_apsta_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "application/ports/driven/itime_source.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

SupervisionLoopInteractor::SupervisionLoopInteractor(OtaValidityAdapter& ota,
                                                        EventLogAdapter& log,
                                                        HttpControllerAdapter& http,
                                                        WifiApStaAdapter& wifi,
                                                        WebPresenterAdapter& web,
                                                        ITimeSource& time)
    : ota_(ota), log_(log), http_(http), wifi_(wifi), web_(web), time_(time) {}

void SupervisionLoopInteractor::tick()
{
    ota_.heartbeat();
    wifi_.try_recover_ap();

    // ── CPU + heap stats ─────────────────────────────────
    cycle_++;
    uint32_t idle0 = ulTaskGetIdleRunTimePercentForCore(0);
    uint32_t idle1 = ulTaskGetIdleRunTimePercentForCore(1);
    uint32_t free_heap = esp_get_free_heap_size();
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);
    uint32_t largest_free = info.largest_free_block;

    ESP_LOGI(TAG, "Аптайм: %lld с, куча: своб=%" PRIu32 " крупн=%" PRIu32
             " блоков: алл=%" PRIu32 " своб=%" PRIu32
             " | CPU: core0=%d%% core1=%d%% total=%d%%",
             time_.monotonic_us() / 1000000,
             free_heap, largest_free,
             (uint32_t)info.allocated_blocks, (uint32_t)info.free_blocks,
             100 - (int)idle0, 100 - (int)idle1,
             (200 - (int)idle0 - (int)idle1) / 2);

    // Обновляем WebPresenter — heap видна в веб-интерфейсе
    web_.set_heap_info(free_heap, largest_free);

    if (cycle_ % 5 == 0) {
        static char stats_buf[1024];
        vTaskGetRunTimeStats(stats_buf);
        ESP_LOGI(TAG, "── Статистика задач (CPU) ──\n%s", stats_buf);
    }

    // ── Recovery ladder ────────────────────────────────────
    static int recovery_level = 0;
    static int http_restart_attempts = 0;
    if (largest_free < 4096 || free_heap < 8192) {
        ESP_LOGE(TAG, "Куча исчерпана (своб=%" PRIu32 " крупн=%" PRIu32 ") — перезагрузка",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Перезагрузка: куча исчерпана (%" PRIu32 "/%" PRIu32 ")",
                   free_heap, largest_free);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else if (largest_free < 6144 && http_restart_attempts < 3) {
        http_restart_attempts++;
        recovery_level = 3;
        ESP_LOGW(TAG, "Recovery L3: перезапуск HTTP (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L3: перезапуск HTTP");
        ota_.set_http_server_up(false);
        http_.stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        ota_.set_http_server_up(http_.start());
    } else if (largest_free < 12288 && recovery_level < 2) {
        recovery_level = 2;
        ESP_LOGW(TAG, "Recovery L2: фрагментация (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L2: фрагментация кучи");
    } else if (largest_free < 16384 && recovery_level < 1) {
        recovery_level = 1;
        ESP_LOGW(TAG, "Recovery L1: предупреждение (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                 free_heap, largest_free);
        log_.event(ILogger::SYSTEM, "Recovery L1: фрагментация растёт");
    }
    if (largest_free >= 16384) { recovery_level = 0; http_restart_attempts = 0; }
}
