#include "infrastructure/driving/wifi_init_adapter.h"
#include "infrastructure/driving/wifi_config.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include <cstring>

const char* WifiInitAdapter::TAG = "wifi";
WifiInitAdapter* WifiInitAdapter::s_self = nullptr;

WifiInitAdapter::WifiInitAdapter()
{
    s_self = this;
}

WifiInitAdapter::~WifiInitAdapter()
{
    s_self = nullptr;
}

void WifiInitAdapter::event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    auto* self = static_cast<WifiInitAdapter*>(arg);
    if (!self) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (self->retry_count_ < MAX_RETRY) {
            esp_wifi_connect();
            self->retry_count_++;
            ESP_LOGI(TAG, "WiFi повтор %d/%d", self->retry_count_, MAX_RETRY);
        } else {
            xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi ошибка после %d попыток", MAX_RETRY);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "WiFi подключён. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Веб: http://" IPSTR, IP2STR(&event->ip_info.ip));
        self->retry_count_ = 0;
        self->connected_ = true;
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
        esp_wifi_set_ps(WIFI_PS_NONE);  // Disable modem sleep for responsive HTTP
    }
}

bool WifiInitAdapter::start()
{
    if (started_) return connected_;

    event_group_ = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, &inst_got_ip));

    wifi_config_t wifi_cfg = {};
    strncpy((char*)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char*)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Подключение к SSID: %s ...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(event_group_,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    started_ = true;
    if (bits & WIFI_CONNECTED_BIT) {
        connected_ = true;
        return true;
    }
    ESP_LOGE(TAG, "WiFi ошибка — перезагрузка через 5 сек");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
    return false;
}
