#pragma once

#include "application/ports/driven/iwifi_hardware.h"  // WifiApRecord

/// High-level WiFi manager consumed by main.cpp and HttpControllerAdapter.
/// Implemented by WifiApStaAdapter in infrastructure/driving/.
class IWifiManager {
public:
    enum class Mode { FIRST_BOOT, STA, AP };

    /// Main boot sequence. Determines mode from stored config,
    /// initialises WiFi accordingly. Does NOT block indefinitely —
    /// if STA fails, transitions to FIRST_BOOT/AP.
    virtual Mode boot() = 0;

    /// Scan for nearby WiFi networks (blocking, ~3-5 sec).
    /// @return number of networks found.
    virtual int scan_networks(WifiApRecord* out, int max) = 0;

    /// Save settings and reboot into the chosen mode.
    /// @param mode      1=STA, 2=AP
    /// @param sta_ssid  SSID (ignored for AP, may be null)
    /// @param sta_pass  password (ignored for AP, may be null)
    /// @param ap_pass   AP password (ignored for STA, may be null)
    virtual void save_settings_and_reboot(int mode,
                                          const char* sta_ssid,
                                          const char* sta_pass,
                                          const char* ap_pass) = 0;

    /// Factory-reset WiFi settings and reboot to FIRST_BOOT (open AP).
    virtual void factory_reset_and_reboot() = 0;

    // ── Getters ───────────────────────────────────

    virtual Mode mode() const = 0;
    virtual const char* sta_ip() const = 0;         // e.g. "192.168.1.42" or "0.0.0.0"
    virtual const char* ap_ip() const = 0;          // always "192.168.4.1"
    virtual const char* connected_ssid() const = 0; // STA SSID or ""
    virtual int rssi() const = 0;                   // dBm or 0

    virtual ~IWifiManager() = default;
};
