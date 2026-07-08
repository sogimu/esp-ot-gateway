#pragma once

#include "application/ports/driven/iwifi_hardware.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/// ESP-IDF implementation of IWifiHardware.
class Esp32WifiAdapter : public IWifiHardware {
public:
    Esp32WifiAdapter();
    ~Esp32WifiAdapter() override;

    bool init() override;
    bool start_ap(const char* ssid, const char* password) override;
    bool stop_ap() override;
    bool start_sta(const char* ssid, const char* password) override;
    bool stop_sta() override;
    bool sta_is_connected() override;
    bool sta_get_ip(char* buf, size_t size) override;
    int  sta_get_rssi() override;
    const char* sta_get_ssid() override;
    int  scan(WifiApRecord* results, int max_results) override;

    /// Archives the scan-in-progress guard for host testing.
    bool is_scan_in_progress() const { return scan_in_progress_; }

private:
    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_  = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    bool sta_connected_ = false;
    int  sta_retry_ = 0;
    esp_timer_handle_t reconnect_timer_ = nullptr;
    int  reconnect_delay_sec_ = 5;  // doubles on each failure, capped
    char sta_ip_[16]   = "0.0.0.0";
    char sta_ssid_[33] = "";
    char ap_ssid_[33]  = "";
    int  sta_rssi_ = 0;

    /// Set true during scan() — see IWifiHardware::scan() contract.
    /// Event handler checks should_auto_connect() before esp_wifi_connect().
    bool scan_in_progress_ = false;

    /// Set true only in STA mode (start_sta), false in AP mode.
    /// Prevents auto-connect when STA interface is only used for scanning.
    bool sta_mode_ = false;

    /// @return true only in STA mode and not during scan.
    /// In APSTA mode the STA interface is for scanning only — never auto-connect.
    bool should_auto_connect() const { return sta_mode_ && !scan_in_progress_; }

    static constexpr int RECONNECT_MIN_SEC = 5;
    static constexpr int RECONNECT_MAX_SEC = 60;
    static constexpr int CONNECT_TIMEOUT_SEC = 30;

    static Esp32WifiAdapter* s_self;
    static void event_handler(void* arg, esp_event_base_t base,
                              int32_t id, void* data);
    static void reconnect_timer_cb(void* arg);
    void schedule_reconnect();
    static const char* TAG;
};
