#include "infrastructure/driven/esp32_wifi_adapter.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <cstring>
#include <cstdio>

const char* Esp32WifiAdapter::TAG = "esp32_wifi";
Esp32WifiAdapter* Esp32WifiAdapter::s_self = nullptr;

// ── Event group bits ──────────────────────────────────────
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_SCAN_DONE_BIT BIT2

Esp32WifiAdapter::Esp32WifiAdapter()
{
    s_self = this;
}

Esp32WifiAdapter::~Esp32WifiAdapter()
{
    s_self = nullptr;
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

void Esp32WifiAdapter::event_handler(void* arg, esp_event_base_t base,
                                     int32_t id, void* data)
{
    auto* self = static_cast<Esp32WifiAdapter*>(arg);
    if (!self) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (self->should_auto_connect()) {
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "STA_START during scan — skipping connect");
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!self->should_auto_connect()) {
            ESP_LOGI(TAG, "STA_DISCONNECTED during scan — ignoring");
        } else if (self->sta_retry_ < MAX_RETRY) {
            esp_wifi_connect();
            self->sta_retry_++;
            ESP_LOGI(TAG, "WiFi повтор %d/%d", self->sta_retry_, MAX_RETRY);
        } else {
            xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi ошибка после %d попыток", MAX_RETRY);
            self->sta_connected_ = false;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        snprintf(self->sta_ip_, sizeof(self->sta_ip_),
                 IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "WiFi подключён. IP: %s", self->sta_ip_);
        self->sta_retry_ = 0;
        self->sta_connected_ = true;
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(self->event_group_, WIFI_SCAN_DONE_BIT);
    }
}

bool Esp32WifiAdapter::init()
{
    event_group_ = xEventGroupCreate();
    if (!event_group_) return false;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, &inst_got_ip));

    ESP_LOGI(TAG, "WiFi инициализирован");
    return true;
}

// ── AP ────────────────────────────────────────────────────

bool Esp32WifiAdapter::start_ap(const char* ssid, const char* password)
{
    if (!ap_netif_) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid_, sizeof(ap_ssid_), "%s-%02X%02X%02X",
             ssid, mac[3], mac[4], mac[5]);

    wifi_config_t cfg = {};
    strncpy((char*)cfg.ap.ssid, ap_ssid_, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.max_connection = 2;

    if (password && password[0] != '\0') {
        strncpy((char*)cfg.ap.password, password, sizeof(cfg.ap.password) - 1);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.channel = 1;

    sta_mode_ = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "AP запущена: %s (auth=%s, max_conn=%d, mode=APSTA)",
             ap_ssid_,
             password && password[0] ? "WPA2" : "open",
             cfg.ap.max_connection);
    return true;
}

bool Esp32WifiAdapter::stop_ap()
{
    esp_wifi_set_mode(WIFI_MODE_NULL);
    ESP_LOGI(TAG, "AP остановлена");
    return true;
}

// ── STA ───────────────────────────────────────────────────

bool Esp32WifiAdapter::start_sta(const char* ssid, const char* password)
{
    strncpy(sta_ssid_, ssid, sizeof(sta_ssid_) - 1);
    sta_connected_ = false;
    sta_retry_ = 0;
    sta_mode_ = true;

    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char*)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    xEventGroupClearBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Подключение к SSID: %s...", ssid);

    EventBits_t bits = xEventGroupWaitBits(event_group_,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(CONNECT_TIMEOUT_SEC * 1000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA подключён. IP: %s", sta_ip_);
        return true;
    }

    ESP_LOGW(TAG, "STA: таймаут подключения к %s", ssid);
    sta_connected_ = false;
    return false;
}

bool Esp32WifiAdapter::stop_sta()
{
    esp_wifi_disconnect();
    sta_connected_ = false;
    sta_mode_ = false;
    ESP_LOGI(TAG, "STA остановлен");
    return true;
}

bool Esp32WifiAdapter::sta_is_connected()
{
    return sta_connected_;
}

bool Esp32WifiAdapter::sta_get_ip(char* buf, size_t size)
{
    if (!sta_connected_) return false;
    strncpy(buf, sta_ip_, size);
    return true;
}

int Esp32WifiAdapter::sta_get_rssi()
{
    if (!sta_connected_) return 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        sta_rssi_ = ap_info.rssi;
    }
    return sta_rssi_;
}

const char* Esp32WifiAdapter::sta_get_ssid()
{
    return sta_connected_ ? sta_ssid_ : "";
}

// ── Scan ──────────────────────────────────────────────────

int Esp32WifiAdapter::scan(WifiApRecord* results, int max_results)
{
    scan_in_progress_ = true;

    wifi_mode_t prev_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&prev_mode);
    ESP_LOGI(TAG, "scan: prev_mode=%d (0=NULL,1=STA,2=AP,3=APSTA)", (int)prev_mode);

    // Phase 1: Fresh STA init via stop→APSTA→start
    // ESP-IDF requires a freshly-initialised STA interface for scan to find networks.
    // AP goes down briefly — async FreeRTOS task + polling in http_controller_adapter
    // handles client reconnection.
    bool need_restore = (prev_mode == WIFI_MODE_AP || prev_mode == WIFI_MODE_APSTA);
    if (need_restore) {
        ESP_LOGI(TAG, "scan: stopping WiFi for mode switch AP->APSTA");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_LOGI(TAG, "scan: set_mode(APSTA) err=0x%x", mode_err);

        esp_err_t start_err = esp_wifi_start();
        ESP_LOGI(TAG, "scan: wifi_start() err=0x%x", start_err);

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Phase 2: Blocking scan with default config (like ESP-IDF example)
    xEventGroupClearBits(event_group_, WIFI_SCAN_DONE_BIT);

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_wifi_scan_start(nullptr, true);  // NULL=defaults, blocking
    int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "scan: scan_start(block,default) returned 0x%x in %lld ms",
             err, dt_ms);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan: start failed err=0x%x", err);
        if (need_restore) {
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            esp_wifi_start();
        }
        scan_in_progress_ = false;
        return 0;
    }

    // Phase 3: Retrieve results (ESP-IDF example order: records first)
    wifi_ap_record_t raw[max_results];
    uint16_t count = (uint16_t)max_results;
    memset(raw, 0, sizeof(raw));

    err = esp_wifi_scan_get_ap_records(&count, raw);
    ESP_LOGI(TAG, "scan: get_ap_records err=0x%x count=%d", err, (int)count);

    if (err != ESP_OK || count == 0) {
        ESP_LOGW(TAG, "scan: no networks found (count=%d, err=0x%x) dt=%lld ms",
                 (int)count, err, dt_ms);
        if (need_restore) {
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            esp_wifi_start();
        }
        scan_in_progress_ = false;
        return 0;
    }

    for (int i = 0; i < count; i++) {
        strncpy(results[i].ssid, (const char*)raw[i].ssid, 32);
        results[i].ssid[32] = '\0';
        results[i].rssi      = raw[i].rssi;
        results[i].auth_mode = raw[i].authmode;
        results[i].channel   = raw[i].primary;
        ESP_LOGI(TAG, "scan:   [%d] ch=%d rssi=%d auth=%d ssid=\"%s\"",
                 i, raw[i].primary, raw[i].rssi, raw[i].authmode, results[i].ssid);
    }

    ESP_LOGI(TAG, "scan: найдено %d сетей за %lld ms", count, dt_ms);

    // Phase 4: Restore APSTA mode
    if (need_restore) {
        ESP_LOGI(TAG, "scan: restoring APSTA mode");
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    scan_in_progress_ = false;
    return count;
}
