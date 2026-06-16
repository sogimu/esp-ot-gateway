#pragma once

#include "application/ports/driven/iwifi_credential_store.h"

/// NVS-backed WiFi credential store.
/// Uses existing "config" namespace. Keys: wifi_mode, wifi_ssid, wifi_pass, ap_pass, sta_fail_cnt.
class WifiNvsAdapter : public IWifiCredentialStore {
public:
    bool load(int& mode,
              char* sta_ssid, size_t ssid_sz,
              char* sta_pass, size_t pass_sz,
              char* ap_pass,  size_t ap_sz) override;
    void save(int mode,
              const char* sta_ssid, const char* sta_pass,
              const char* ap_pass) override;
    void erase() override;
    uint8_t sta_fail_count() override;
    void set_sta_fail_count(uint8_t count) override;

private:
    static const char* TAG;
    static const char* NVS_NS;  // "config"
};
