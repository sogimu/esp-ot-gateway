#pragma once

#include <cstddef>
#include <cstdint>

/// WiFi scan result (ESP-IDF-free DTO).
struct WifiApRecord {
    char    ssid[33];
    int8_t  rssi;       // dBm, e.g. -45
    uint8_t auth_mode;  // 0=Open, 1=WEP, 2=WPA_PSK, 3=WPA2_PSK,
                        // 4=WPA_WPA2_PSK, 5=WPA2_ENTERPRISE, 6=WPA3_PSK
    uint8_t channel;    // 1-14
};

/// Low-level WiFi hardware abstraction.
/// Implemented by Esp32WifiAdapter in infrastructure/driven/.
class IWifiHardware {
public:
    /// One-time init: netif, event loop, wifi stack.
    /// Must be called before any other method.
    virtual bool init() = 0;

    // ── Access Point ──────────────────────────────

    /// Start SoftAP with given SSID and password.
    /// @param ssid      AP SSID (max 32 chars). MAC suffix appended by implementation.
    /// @param password  nullptr or empty = open AP. Otherwise WPA2-PSK.
    /// @return true on success.
    virtual bool start_ap(const char* ssid, const char* password) = 0;

    /// Stop SoftAP.
    virtual bool stop_ap() = 0;

    // ── Station ───────────────────────────────────

    /// Connect to an AP as station.
    /// @param ssid      target AP SSID
    /// @param password  target AP password (WPA2-PSK)
    /// @return true if connection initiated and completed successfully.
    virtual bool start_sta(const char* ssid, const char* password) = 0;

    /// Disconnect station.
    virtual bool stop_sta() = 0;

    /// Is station currently connected?
    virtual bool sta_is_connected() = 0;

    /// Copy station IP address string (e.g. "192.168.1.42") into buf.
    /// Returns false if not connected.
    virtual bool sta_get_ip(char* buf, size_t size) = 0;

    /// Get station RSSI in dBm. Returns 0 if not connected.
    virtual int sta_get_rssi() = 0;

    /// Get connected AP SSID. Returns "" if not connected.
    virtual const char* sta_get_ssid() = 0;

    // ── Scanning ──────────────────────────────────

    /// Perform a blocking WiFi scan. Stores up to max_results in results.
    /// @return number of APs found (0 if none).
    /// Blocks for ~3-5 seconds. Must be called from a task context.
    ///
    /// IMPLEMENTATION CONTRACT: The implementation MUST prevent any automatic
    /// WiFi connection attempts (e.g. esp_wifi_connect() from event handlers)
    /// while a scan is in progress. Concurrent connect+scan causes the radio
    /// to be busy and the scan to return 0 results silently (ESP-IDF bug).
    /// Use a scoped guard flag (e.g. scan_in_progress_) checked in the
    /// WIFI_EVENT_STA_START / WIFI_EVENT_STA_DISCONNECTED handlers.
    virtual int scan(WifiApRecord* results, int max_results) = 0;

    virtual ~IWifiHardware() = default;
};
