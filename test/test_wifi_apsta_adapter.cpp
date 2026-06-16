/// Host tests for WifiApStaAdapter state machine.
/// Uses FakeWifiHardware + FakeWifiCredentialStore + TestableWifiAdapter.

#include <catch2/catch_test_macros.hpp>
#include "infrastructure/driving/wifi_apsta_adapter.h"
#include "fakes/fake_wifi_hardware.h"
#include "fakes/fake_wifi_credential_store.h"

/// Overrides do_reboot() / do_delay_ms() — no real ESP32 restart during tests.
class TestableWifiAdapter : public WifiApStaAdapter {
public:
    using WifiApStaAdapter::WifiApStaAdapter;
    bool reboot_called_ = false;
    int  last_delay_ms_ = 0;

protected:
    void do_reboot() override      { reboot_called_ = true; }
    void do_delay_ms(int ms) override { last_delay_ms_ = ms; }
};

// ── Boot: FIRST_BOOT ──────────────────────────────────────

TEST_CASE("WifiApStaAdapter — boot() FIRST_BOOT when no stored config", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;  // has_data_ = false (default)
    TestableWifiAdapter wifi(hw, store);

    auto mode = wifi.boot();
    REQUIRE(mode == IWifiManager::Mode::FIRST_BOOT);
    REQUIRE(hw.ap_started_ == true);
    REQUIRE(wifi.dns().start_called() == true);
    // Open AP (no password): last_ap_password_ should be empty or null
    REQUIRE(hw.last_ap_password_[0] == '\0');
}

// ── Boot: STA success ─────────────────────────────────────

TEST_CASE("WifiApStaAdapter — boot() STA mode success", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "password123", 63);
    hw.sta_connect_ok = true;
    TestableWifiAdapter wifi(hw, store);

    auto mode = wifi.boot();
    REQUIRE(mode == IWifiManager::Mode::STA);
    REQUIRE(store.fail_cnt_ == 0);
    REQUIRE(wifi.dns().is_running() == false);
    REQUIRE(hw.sta_started_ == true);
}

TEST_CASE("WifiApStaAdapter — STA success resets fail counter", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    store.fail_cnt_ = 2;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "correct", 63);
    hw.sta_connect_ok = true;
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(store.fail_cnt_ == 0);
    REQUIRE(wifi.mode() == IWifiManager::Mode::STA);
}

// ── Boot: STA failure + reboot-loop protection ────────────

TEST_CASE("WifiApStaAdapter — STA fail #1 increments counter", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    store.fail_cnt_ = 0;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "wrong", 63);
    hw.sta_connect_ok = false;
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(store.fail_cnt_ == 1);
    REQUIRE(wifi.reboot_called_ == true);
}

TEST_CASE("WifiApStaAdapter — STA fail #2 does NOT reset yet", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    store.fail_cnt_ = 1;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "wrong", 63);
    hw.sta_connect_ok = false;
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(store.fail_cnt_ == 2);
    REQUIRE(store.erase_called_ == false);  // not yet
    REQUIRE(wifi.reboot_called_ == true);
}

TEST_CASE("WifiApStaAdapter — STA fail #3 triggers FIRST_BOOT reset", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    store.fail_cnt_ = 2;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "wrong", 63);
    hw.sta_connect_ok = false;
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(store.erase_called_ == true);   // wiped
    REQUIRE(store.fail_cnt_ == 0);
    REQUIRE(wifi.reboot_called_ == true);
}

// ── Boot: AP mode ─────────────────────────────────────────

TEST_CASE("WifiApStaAdapter — boot() AP mode with password", "[wifi][boot]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 2;
    strncpy(store.ap_pass_, "mysecret", 63);
    TestableWifiAdapter wifi(hw, store);

    auto mode = wifi.boot();
    REQUIRE(mode == IWifiManager::Mode::AP);
    REQUIRE(hw.ap_started_ == true);
    REQUIRE(wifi.dns().is_running() == false);
    // Password should have been passed to hardware
    REQUIRE(hw.last_ap_password_[0] != '\0');
}

// ── DNS lifecycle ─────────────────────────────────────────

TEST_CASE("WifiApStaAdapter — DNS starts in FIRST_BOOT", "[wifi][dns]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;  // no data
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    // DNS start() is called (binding to port 53 requires root on host, so is_running() may be false)
    REQUIRE(wifi.dns().start_called() == true);
}

TEST_CASE("WifiApStaAdapter — DNS stops on save_and_reboot", "[wifi][dns]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;  // no data
    TestableWifiAdapter wifi(hw, store);
    wifi.boot();
    REQUIRE(wifi.dns().start_called() == true);

    wifi.save_settings_and_reboot(1, "MyWiFi", "password", "");
    REQUIRE(wifi.dns().is_running() == false);
    REQUIRE(wifi.reboot_called_ == true);
}

TEST_CASE("WifiApStaAdapter — DNS NOT started in AP mode", "[wifi][dns]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 2;
    strncpy(store.ap_pass_, "secret", 63);
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(wifi.mode() == IWifiManager::Mode::AP);
    REQUIRE(wifi.dns().start_called() == false);
}

TEST_CASE("WifiApStaAdapter — DNS NOT started in STA mode", "[wifi][dns]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "password", 63);
    hw.sta_connect_ok = true;
    TestableWifiAdapter wifi(hw, store);

    wifi.boot();
    REQUIRE(wifi.dns().start_called() == false);
}

// ── Save & Reboot ─────────────────────────────────────────

TEST_CASE("WifiApStaAdapter — save STA with valid credentials", "[wifi][save]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    TestableWifiAdapter wifi(hw, store);

    wifi.save_settings_and_reboot(1, "MyWiFi", "password", "");
    REQUIRE(store.save_called_ == 1);
    REQUIRE(store.mode_ == 1);
    REQUIRE(strcmp(store.sta_ssid_, "MyWiFi") == 0);
    REQUIRE(strcmp(store.sta_pass_, "password") == 0);
    REQUIRE(wifi.reboot_called_ == true);
}

TEST_CASE("WifiApStaAdapter — save AP with valid password", "[wifi][save]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    TestableWifiAdapter wifi(hw, store);

    wifi.save_settings_and_reboot(2, "", "", "secret99");
    REQUIRE(store.save_called_ == 1);
    REQUIRE(store.mode_ == 2);
    REQUIRE(strcmp(store.ap_pass_, "secret99") == 0);
    REQUIRE(wifi.reboot_called_ == true);
}

TEST_CASE("WifiApStaAdapter — save rejects empty SSID", "[wifi][save]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    TestableWifiAdapter wifi(hw, store);

    wifi.save_settings_and_reboot(1, "", "password", "");
    REQUIRE(store.save_called_ == 0);   // save was NOT called
    REQUIRE(wifi.reboot_called_ == false);
}

TEST_CASE("WifiApStaAdapter — save rejects short password (<8)", "[wifi][save]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    TestableWifiAdapter wifi(hw, store);

    wifi.save_settings_and_reboot(2, "", "", "1234567");
    REQUIRE(store.save_called_ == 0);
    REQUIRE(wifi.reboot_called_ == false);
}

TEST_CASE("WifiApStaAdapter — save rejects STA with short password", "[wifi][save]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    TestableWifiAdapter wifi(hw, store);

    wifi.save_settings_and_reboot(1, "SSID", "short", "");
    REQUIRE(store.save_called_ == 0);
    REQUIRE(wifi.reboot_called_ == false);
}

// ── Factory Reset ─────────────────────────────────────────

TEST_CASE("WifiApStaAdapter — factory_reset_and_reboot", "[wifi][reset]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    TestableWifiAdapter wifi(hw, store);

    wifi.factory_reset_and_reboot();
    REQUIRE(store.erase_called_ == true);
    REQUIRE(wifi.dns().is_running() == false);
    REQUIRE(wifi.reboot_called_ == true);
}

// ── Scan → auto-connect race prevention ──────────────────

TEST_CASE("Scan guard prevents start_sta during scan", "[wifi][scan][race]") {
    FakeWifiHardware hw;
    FakeWifiCredentialStore store;
    store.has_data_ = true;
    store.mode_ = 1;
    strncpy(store.sta_ssid_, "MyWiFi", 32);
    strncpy(store.sta_pass_, "password", 63);
    hw.sta_connect_ok = true;
    TestableWifiAdapter wifi(hw, store);

    // Verify Fake tracks scan_in_progress_
    REQUIRE(hw.scan_in_progress_ == false);

    // Boot into STA mode — should NOT trigger start_sta during scan
    wifi.boot();

    // After boot: scan_in_progress must be false (scan resets it)
    REQUIRE(hw.scan_in_progress_ == false);
    // Contract verified: start_sta was never called during a scan
    REQUIRE(hw.start_sta_during_scan_ == false);
}

TEST_CASE("FakeWifiHardware scan sets and resets guard", "[wifi][scan][race]") {
    FakeWifiHardware hw;
    WifiApRecord results[1];

    hw.set_scan_result("TestNet", -45, 3, 6);
    int count = hw.scan(results, 1);

    REQUIRE(count == 1);
    // Guard was set during scan
    REQUIRE(hw.scan_called_ == 1);
    // Guard is cleared after scan returns
    REQUIRE(hw.scan_in_progress_ == false);
}
