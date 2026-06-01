#include <sys/time.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "model/model.h"
#include "controller/controller.h"
#include "endpoints/endpoints.h"
#include "log/log_service.h"
#include "stats/stats_service.h"
#include "predict/predict_service.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "=== Газовый котёл Baxi duo-tec compact ===");
    ESP_LOGI(TAG, "MVC Architecture");

    Model model;
    Endpoints endpoints;

    LogService   log_service(model);
    StatsService stats_service(model, endpoints.ot_);
    PredictService predict_service(model);
    Controller   controller(model, endpoints, log_service, stats_service);

    stats_service.start();
    predict_service.start(endpoints.ot_);
    controller.start();
    endpoints.start();

    log_service.event(LOG_CAT_SYSTEM, "Система запущена");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));

        int idle0 = (int)ulTaskGetIdleRunTimePercentForCore(0);
        int idle1 = (int)ulTaskGetIdleRunTimePercentForCore(1);
        int load0 = 100 - idle0;
        int load1 = 100 - idle1;

        ESP_LOGI(TAG, "Uptime: %lld s, free heap: %" PRIu32 " | CPU: core0=%d%% core1=%d%% total=%d%%",
                 esp_timer_get_time() / 1000000,
                 esp_get_free_heap_size(),
                 load0, load1, (load0 + load1) / 2);
    }
}
