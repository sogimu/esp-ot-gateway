#include "infrastructure/driving/wifi_apsta_adapter.h"
#include "application/ports/driven/iwifi_hardware.h"
#include "application/ports/driven/iwifi_credential_store.h"
#include "domain/value_objects/wifi_validation.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

const char* WifiApStaAdapter::TAG = "wifi";
const char* WifiApStaAdapter::AP_SSID_BASE = "Baxi-OT-Setup";

WifiApStaAdapter::WifiApStaAdapter(IWifiHardware& hw, IWifiCredentialStore& store)
    : hw_(hw), store_(store)
{}

// ── Boot ──────────────────────────────────────────────────

IWifiManager::Mode WifiApStaAdapter::boot()
{
    // 1. Init hardware
    if (!hw_.init()) {
        ESP_LOGE(TAG, "WiFi: ошибка инициализации");
        boot_first_boot();  // last resort: try open AP
        return (mode_ = Mode::FIRST_BOOT);
    }

    // 2. Load stored config
    int stored_mode = 0;
    char sta_ssid[33] = {}, sta_pass[65] = {}, ap_pass[65] = {};
    if (!store_.load(stored_mode, sta_ssid, sizeof(sta_ssid),
                     sta_pass, sizeof(sta_pass), ap_pass, sizeof(ap_pass))) {
        ESP_LOGI(TAG, "WiFi: нет сохранённых настроек — FIRST_BOOT");
        boot_first_boot();
        return (mode_ = Mode::FIRST_BOOT);
    }

    // 3. Integrity check
    if (stored_mode == 1 && sta_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi: режим STA но SSID пуст — сброс в FIRST_BOOT");
        store_.erase();
        boot_first_boot();
        return (mode_ = Mode::FIRST_BOOT);
    }
    if (stored_mode == 2 && ap_pass[0] == '\0') {
        ESP_LOGW(TAG, "WiFi: режим AP но пароль пуст — сброс в FIRST_BOOT");
        store_.erase();
        boot_first_boot();
        return (mode_ = Mode::FIRST_BOOT);
    }

    // 4. Branch by mode
    if (stored_mode == 1) {
        boot_sta();
    } else if (stored_mode == 2) {
        boot_ap(ap_pass);
    } else {
        ESP_LOGW(TAG, "WiFi: неизвестный режим %d — FIRST_BOOT", stored_mode);
        boot_first_boot();
    }
    return mode_;
}

void WifiApStaAdapter::boot_first_boot()
{
    ESP_LOGI(TAG, "WiFi: FIRST_BOOT — открытая AP для настройки");
    hw_.start_ap(AP_SSID_BASE, /*password=*/nullptr);
    dns_.start();
    mode_ = Mode::FIRST_BOOT;
}

void WifiApStaAdapter::boot_sta()
{
    int stored_mode;
    char ssid[33], pass[65], ap_pass[65];
    store_.load(stored_mode, ssid, sizeof(ssid), pass, sizeof(pass),
                ap_pass, sizeof(ap_pass));

    ESP_LOGI(TAG, "WiFi: подключение к %s...", ssid);

    if (hw_.start_sta(ssid, pass)) {
        ESP_LOGI(TAG, "WiFi: подключён к %s", ssid);
        store_.set_sta_fail_count(0);
        mode_ = Mode::STA;
        return;
    }

    // Failure — increment counter
    uint8_t fail_cnt = store_.sta_fail_count() + 1;
    store_.set_sta_fail_count(fail_cnt);
    ESP_LOGW(TAG, "WiFi: ошибка подключения (попытка %d/%d)",
             fail_cnt, STA_FAIL_REBOOT_MAX);

    if (fail_cnt >= STA_FAIL_REBOOT_MAX) {
        ESP_LOGE(TAG, "WiFi: %d неудачных загрузок подряд — сброс в FIRST_BOOT",
                 STA_FAIL_REBOOT_MAX);
        store_.erase();
        store_.set_sta_fail_count(0);
    }

    do_delay_ms(500);
    do_reboot();
}

void WifiApStaAdapter::boot_ap(const char* ap_password)
{
    ESP_LOGI(TAG, "WiFi: запуск AP с паролем...");
    hw_.start_ap(AP_SSID_BASE, ap_password);
    // DNS NOT started — captive portal not needed in normal AP mode
    mode_ = Mode::AP;
}

// ── Scan ──────────────────────────────────────────────────

int WifiApStaAdapter::scan_networks(WifiApRecord* out, int max)
{
    return hw_.scan(out, max);
}

// ── Save & Reboot ─────────────────────────────────────────

void WifiApStaAdapter::save_settings_and_reboot(
        int mode, const char* sta_ssid, const char* sta_pass,
        const char* ap_pass)
{
    // Validate
    if (mode == 1) {
        auto r = validate_ssid(sta_ssid);
        if (!r.ok) {
            ESP_LOGE(TAG, "Сохранение: %s", r.error);
            return;
        }
        r = validate_wifi_password(sta_pass);
        if (!r.ok) {
            ESP_LOGE(TAG, "Сохранение: %s", r.error);
            return;
        }
    } else if (mode == 2) {
        auto r = validate_ap_password(ap_pass);
        if (!r.ok) {
            ESP_LOGE(TAG, "Сохранение: %s", r.error);
            return;
        }
    } else {
        ESP_LOGE(TAG, "Сохранение: некорректный режим %d", mode);
        return;
    }

    // Stop DNS (no longer needed after provisioning)
    dns_.stop();

    // Save to NVS
    store_.save(mode, sta_ssid, sta_pass, ap_pass);
    ESP_LOGI(TAG, "WiFi: настройки сохранены (mode=%d), перезагрузка...", mode);

    do_delay_ms(500);
    do_reboot();
}

void WifiApStaAdapter::factory_reset_and_reboot()
{
    dns_.stop();
    store_.erase();
    store_.set_sta_fail_count(0);
    ESP_LOGI(TAG, "WiFi: сброс в FIRST_BOOT, перезагрузка...");
    do_delay_ms(500);
    do_reboot();
}

// ── Getters ───────────────────────────────────────────────

const char* WifiApStaAdapter::sta_ip() const
{
    if (mode_ == Mode::STA) {
        const_cast<WifiApStaAdapter*>(this)->hw_.sta_get_ip(sta_ip_buf_, sizeof(sta_ip_buf_));
    }
    return sta_ip_buf_;
}

const char* WifiApStaAdapter::connected_ssid() const
{
    return const_cast<WifiApStaAdapter*>(this)->hw_.sta_get_ssid();
}

int WifiApStaAdapter::rssi() const
{
    return const_cast<WifiApStaAdapter*>(this)->hw_.sta_get_rssi();
}

// ── AP Watchdog ──────────────────────────────────────────────

void WifiApStaAdapter::try_recover_ap()
{
    // Only relevant in AP / FIRST_BOOT modes
    if (mode_ != Mode::AP && mode_ != Mode::FIRST_BOOT) return;

    wifi_mode_t current = WIFI_MODE_NULL;
    esp_wifi_get_mode(&current);

    // With Fix 1, AP runs in APSTA mode (3). If WiFi is dead (0=NULL),
    // restart AP from stored credentials.
    if (current == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "AP watchdog: WiFi mode=NULL — перезапуск AP");

        int stored_mode;
        char ssid[33], pass[65], ap_pass[65];
        if (store_.load(stored_mode, ssid, sizeof(ssid),
                        pass, sizeof(pass), ap_pass, sizeof(ap_pass))) {
            const char* pw = (ap_pass[0] != '\0') ? ap_pass : nullptr;
            hw_.start_ap(AP_SSID_BASE, pw);
        } else {
            hw_.start_ap(AP_SSID_BASE, /*open*/ nullptr);
        }

        // Restart DNS if in FIRST_BOOT
        if (mode_ == Mode::FIRST_BOOT && !dns_.is_running()) {
            dns_.start();
        }
    }
}

// ── Protected overridables ────────────────────────────────

void WifiApStaAdapter::do_reboot()
{
    esp_restart();
}

void WifiApStaAdapter::do_delay_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
