#pragma once

#include "application/ports/driven/iwifi_hardware.h"
#include <cstring>

/// Fake IWifiHardware for host unit testing.
/// All behaviour is controlled via public fields — set them before the test,
/// check them after.
class FakeWifiHardware : public IWifiHardware {
public:
    // ── Configuration (set before test) ──────────────
    bool init_ok = true;
    bool ap_ok = true;
    bool sta_connect_ok = true;

    // ── State tracking (check after test) ────────────
    bool init_called_ = false;
    bool ap_started_ = false;
    bool ap_stopped_ = false;
    bool sta_started_ = false;
    bool sta_stopped_ = false;
    int  scan_called_ = 0;
    bool sta_is_connected_ = false;
    bool scan_in_progress_ = false;      // matches Esp32WifiAdapter contract
    bool start_sta_during_scan_ = false; // caught contract violation

    // Last arguments passed to methods
    char last_ap_ssid_[33] = {};
    char last_ap_password_[65] = {};
    char last_sta_ssid_[33] = {};
    char last_sta_password_[65] = {};

    // Return values for getters
    char sta_ip_buf_[16] = "192.168.1.42";
    char sta_ssid_buf_[33] = "TestNet";
    int  sta_rssi_ = -45;

    // Scan results
    WifiApRecord scan_results_[20] = {};
    int scan_count_ = 0;

    // ── Helpers ───────────────────────────────────────
    void reset() {
        init_ok = true; ap_ok = true; sta_connect_ok = true;
        init_called_ = false; ap_started_ = false; ap_stopped_ = false;
        sta_started_ = false; sta_stopped_ = false; scan_called_ = 0;
        sta_is_connected_ = false; scan_count_ = 0;
        scan_in_progress_ = false; start_sta_during_scan_ = false;
        memset(last_ap_ssid_, 0, 33); memset(last_ap_password_, 0, 65);
        memset(last_sta_ssid_, 0, 33); memset(last_sta_password_, 0, 65);
        strncpy(sta_ip_buf_, "192.168.1.42", 15);
        strncpy(sta_ssid_buf_, "TestNet", 32);
        sta_rssi_ = -45;
    }

    void set_scan_result(const char* ssid, int rssi, uint8_t auth, uint8_t ch) {
        if (scan_count_ < 20) {
            strncpy(scan_results_[scan_count_].ssid, ssid, 32);
            scan_results_[scan_count_].rssi = (int8_t)rssi;
            scan_results_[scan_count_].auth_mode = auth;
            scan_results_[scan_count_].channel = ch;
            scan_count_++;
        }
    }

    // ── IWifiHardware implementation ─────────────────
    bool init() override { init_called_ = true; return init_ok; }

    bool start_ap(const char* ssid, const char* password) override {
        if (ssid) strncpy(last_ap_ssid_, ssid, 32);
        if (password) strncpy(last_ap_password_, password, 63);
        ap_started_ = true;
        return ap_ok;
    }

    bool stop_ap() override { ap_stopped_ = true; ap_started_ = false; return true; }

    bool start_sta(const char* ssid, const char* password) override {
        if (scan_in_progress_) start_sta_during_scan_ = true;  // contract violation!
        if (ssid) strncpy(last_sta_ssid_, ssid, 32);
        if (password) strncpy(last_sta_password_, password, 63);
        sta_started_ = true;
        sta_is_connected_ = sta_connect_ok;
        return sta_connect_ok;
    }

    bool stop_sta() override { sta_stopped_ = true; sta_is_connected_ = false; return true; }

    bool sta_is_connected() override { return sta_is_connected_; }

    bool sta_get_ip(char* buf, size_t size) override {
        if (!sta_is_connected_) return false;
        strncpy(buf, sta_ip_buf_, size);
        return true;
    }

    int sta_get_rssi() override { return sta_is_connected_ ? sta_rssi_ : 0; }

    const char* sta_get_ssid() override { return sta_is_connected_ ? sta_ssid_buf_ : ""; }

    int scan(WifiApRecord* results, int max_results) override {
        scan_in_progress_ = true;
        scan_called_++;
        int n = scan_count_ < max_results ? scan_count_ : max_results;
        memcpy(results, scan_results_, n * sizeof(WifiApRecord));
        scan_in_progress_ = false;
        return n;
    }
};
