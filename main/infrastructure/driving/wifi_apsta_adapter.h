#pragma once

#include "application/ports/driving/iwifi_manager.h"
#include "infrastructure/driven/dns_captive_server.h"

class IWifiHardware;
class IWifiCredentialStore;

/// Central WiFi state machine adapter.
/// Implements IWifiManager, depends on IWifiHardware + IWifiCredentialStore.
class WifiApStaAdapter : public IWifiManager {
public:
    WifiApStaAdapter(IWifiHardware& hw, IWifiCredentialStore& store);

    Mode boot() override;
    int  scan_networks(WifiApRecord* out, int max) override;
    void save_settings_and_reboot(int mode, const char* sta_ssid,
                                  const char* sta_pass, const char* ap_pass) override;
    void factory_reset_and_reboot() override;

    Mode mode() const override { return mode_; }
    const char* sta_ip() const override;
    const char* ap_ip() const override { return "192.168.4.1"; }
    const char* connected_ssid() const override;
    int  rssi() const override;

    /// Expose DNS for testing and main.cpp
    DnsCaptiveServer& dns() { return dns_; }

    /// Check if AP is alive; if WiFi mode is wrong, restart AP from stored config.
    /// Called periodically from main idle loop (~every 60s). No-op in STA mode.
    void try_recover_ap();

protected:
    /// Overridable for host testing (avoids calling real esp_restart/vTaskDelay).
    virtual void do_reboot();
    virtual void do_delay_ms(int ms);

private:
    IWifiHardware& hw_;
    IWifiCredentialStore& store_;
    DnsCaptiveServer dns_;
    Mode mode_ = Mode::FIRST_BOOT;
    mutable char sta_ip_buf_[16] = "0.0.0.0";

    static constexpr int STA_FAIL_REBOOT_MAX = 3;
    static const char* AP_SSID_BASE;  // "Baxi-OT-Setup"
    static const char* TAG;

    void boot_first_boot();
    void boot_sta();
    void boot_ap(const char* ap_password);
};
