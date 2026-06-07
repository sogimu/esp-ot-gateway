#include <sys/time.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "model/model.h"
#include "controller/controller.h"
#include "endpoints/endpoints.h"
#include "log/log_service.h"
#include "crash/crash_service.h"
#include "stats/stats_service.h"
#include "predict/predict_service.h"
#include "pid/pid_service.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Газовый котёл Baxi duo-tec compact ===");
    ESP_LOGI(TAG, "MVC Architecture");

    Model model;
    Endpoints endpoints;

    endpoints.config_.init();

    LogService   log_service(model);
    CrashService crash_service(log_service);
    StatsService stats_service(model, endpoints.ot_);
    PredictService predict_service(model);
    PidService pid_service(model, endpoints.ot_, endpoints.sensors_);
    Controller   controller(model, endpoints, log_service, stats_service, pid_service);

    crash_service.start();
    stats_service.start(endpoints.config_);
    predict_service.start(endpoints.ot_, endpoints.config_);
    pid_service.start();
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
