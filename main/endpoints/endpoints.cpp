#include "endpoints.h"
#include "esp_log.h"

static const char* TAG = "endpoints";

Endpoints::Endpoints() {}

void Endpoints::start()
{
    ESP_LOGI(TAG, "Запуск всех endpoints...");
    wifi_.start();
    sntp_.start();
    ot_.start();
    web_.start();
    sensors_.start();
    ESP_LOGI(TAG, "Все endpoints запущены");
}

void Endpoints::stop()
{
    ot_.stop();
    web_.stop();
    sensors_.stop();
    sntp_.stop();
}