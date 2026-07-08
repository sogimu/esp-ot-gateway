#pragma once

#include <cstddef>
#include <cstdint>

/// NVS-backed storage for WiFi configuration.
/// Implemented by WifiNvsStore in infrastructure/driven/.
class IWifiCredentialStore {
public:
    /// Load WiFi configuration from storage.
    /// @param mode      out: 0=not configured (first boot), 1=STA, 2=AP
    /// @param sta_ssid  out: buffer for STA SSID (max 33 bytes)
    /// @param ssid_sz   size of sta_ssid buffer
    /// @param sta_pass  out: buffer for STA password (max 65 bytes)
    /// @param pass_sz   size of sta_pass buffer
    /// @param ap_pass   out: buffer for AP password (max 65 bytes)
    /// @param ap_sz     size of ap_pass buffer
    /// @return true if configuration exists, false if first boot (no keys)
    virtual bool load(int& mode,
                      char* sta_ssid, size_t ssid_sz,
                      char* sta_pass, size_t pass_sz,
                      char* ap_pass,  size_t ap_sz) = 0;

    /// Save WiFi configuration to storage.
    /// @param mode      1=STA, 2=AP
    /// @param sta_ssid  SSID (ignored if mode==AP, may be null)
    /// @param sta_pass  password (ignored if mode==AP, may be null)
    /// @param ap_pass   AP password (ignored if mode==STA, may be null)
    virtual void save(int mode,
                      const char* sta_ssid, const char* sta_pass,
                      const char* ap_pass) = 0;

    /// Erase all WiFi keys (factory reset network settings).
    virtual void erase() = 0;

    /// Reboot-loop protection: read consecutive STA failure count.
    virtual uint8_t sta_fail_count() = 0;

    /// Reboot-loop protection: write consecutive STA failure count.
    virtual void set_sta_fail_count(uint8_t count) = 0;

    virtual ~IWifiCredentialStore() = default;
};
