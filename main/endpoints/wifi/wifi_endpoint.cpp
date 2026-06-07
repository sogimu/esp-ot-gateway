#include "wifi_endpoint.h"
#include "wifi_config.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"

#include <cstring>

static const char* TAG = "wifi";

WifiEndpoint* WifiEndpoint::s_self = nullptr;

WifiEndpoint::WifiEndpoint()
    : event_group_(nullptr)
    , retry_count_(0)
    , started_(false)
    , connected_(false)
{
    s_self = this;
}

WifiEndpoint::~WifiEndpoint()
{
    stop();
    if (s_self == this) s_self = nullptr;
}

void WifiEndpoint::event_handler(void* arg, esp_event_base_t base,
                                 int32_t id, void* data)
{
    auto* self = s_self;
    if (!self) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (self->retry_count_ < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            self->retry_count_++;
            ESP_LOGW(TAG, "WiFi переподключение... попытка %d/%d",
                     self->retry_count_, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi: не удалось подключиться");
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "WiFi подключён. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web интерфейс: http://" IPSTR, IP2STR(&event->ip_info.ip));
        self->retry_count_ = 0;
        self->connected_ = true;
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
}

bool WifiEndpoint::start()
{
    if (started_) return connected_;

    event_group_ = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,    event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid,      WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_config.sta.password,  WIFI_PASS, sizeof(WIFI_PASS));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "Подключение к WiFi SSID: %s ...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(event_group_,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    started_ = true;

    if (bits & WIFI_CONNECTED_BIT) return true;

    ESP_LOGE(TAG, "WiFi не подключён — перезагрузка через 5 сек");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
    return false;
}

void WifiEndpoint::stop()
{
    if (!started_) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_loop_delete_default();
    esp_netif_deinit();
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
    started_ = false;
    connected_ = false;
    ESP_LOGI(TAG, "WiFi остановлен");
}

bool WifiEndpoint::is_connected() const
{
    return connected_;
}