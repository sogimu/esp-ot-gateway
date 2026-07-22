#include "infrastructure/driven/wifi_nvs_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>

const char* WifiNvsStore::TAG   = "wifi_nvs";
const char* WifiNvsStore::NVS_NS = "config";

// D10: двухшаговое чтение строкового блоба. Сначала запрос размера через
// nvs_get_blob с NULL, затем чтение только если размер помещается в буфер.
// Возвращает ESP_OK только при успехе; гарантирует null-терминатор. Блоб чужого
// (большего) размера → buf[0]='\0' (дефолт).
static esp_err_t wifi_load_str_blob(nvs_handle_t h, const char* key,
                                     char* buf, size_t buf_sz)
{
    buf[0] = '\0';
    size_t sz = 0;
    esp_err_t r = nvs_get_blob(h, key, nullptr, &sz);
    if (r != ESP_OK) return r;
    if (sz > buf_sz) return ESP_ERR_NVS_INVALID_LENGTH;
    r = nvs_get_blob(h, key, buf, &sz);
    buf[buf_sz - 1] = '\0';
    return r;
}

void WifiNvsStore::init()
{
    nvs_flash_init();
}

bool WifiNvsStore::load(int& mode,
                          char* sta_ssid, size_t ssid_sz,
                          char* sta_pass, size_t pass_sz,
                          char* ap_pass,  size_t ap_sz)
{
    // Zero out buffers
    if (sta_ssid) sta_ssid[0] = '\0';
    if (sta_pass) sta_pass[0] = '\0';
    if (ap_pass)  ap_pass[0]  = '\0';
    mode = 0;

    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Не удалось открыть NVS namespace '%s'", NVS_NS);
        return false;
    }

    uint8_t raw_mode = 0;
    esp_err_t err = nvs_get_u8(handle, "wifi_mode", &raw_mode);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Ключ wifi_mode отсутствует — first boot");
        nvs_close(handle);
        return false;
    }
    mode = raw_mode;
    ESP_LOGI(TAG, "Загружен wifi_mode=%d", mode);

    // D10: Load SSID/пароли через двухшаговый запрос размера (wifi_load_str_blob).
    // Чужой (больший) размер → пустая строка (дефолт); null-терминатор гарантирован.
    err = wifi_load_str_blob(handle, "wifi_ssid", sta_ssid, ssid_sz);
    if (err != ESP_OK && mode == 1) {
        ESP_LOGW(TAG, "Режим STA но SSID отсутствует — сброс в first boot");
        nvs_close(handle);
        mode = 0;
        return false;
    }

    // Load STA password
    err = wifi_load_str_blob(handle, "wifi_pass", sta_pass, pass_sz);
    if (err != ESP_OK && mode == 1) {
        ESP_LOGW(TAG, "Режим STA но пароль отсутствует — сброс в first boot");
        nvs_close(handle);
        mode = 0;
        return false;
    }

    // Load AP password
    err = wifi_load_str_blob(handle, "ap_pass", ap_pass, ap_sz);
    if (err != ESP_OK && mode == 2) {
        ESP_LOGW(TAG, "Режим AP но пароль отсутствует — сброс в first boot");
        nvs_close(handle);
        mode = 0;
        return false;
    }

    nvs_close(handle);

    // Integrity check after load: if mode=STA but SSID is empty → first boot
    if (mode == 1 && sta_ssid[0] == '\0') {
        ESP_LOGW(TAG, "Повреждённые данные: mode=STA, SSID пуст");
        mode = 0;
        return false;
    }
    if (mode == 2 && ap_pass[0] == '\0') {
        ESP_LOGW(TAG, "Повреждённые данные: mode=AP, пароль пуст");
        mode = 0;
        return false;
    }

    return true;
}

void WifiNvsStore::save(int mode,
                          const char* sta_ssid, const char* sta_pass,
                          const char* ap_pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Не удалось открыть NVS для записи: %d", err);
        return;
    }

    nvs_set_u8(handle, "wifi_mode", (uint8_t)mode);

    if (mode == 1 && sta_ssid) {
        nvs_set_blob(handle, "wifi_ssid", sta_ssid, strlen(sta_ssid) + 1);
        if (sta_pass) {
            nvs_set_blob(handle, "wifi_pass", sta_pass, strlen(sta_pass) + 1);
        }
    } else {
        nvs_erase_key(handle, "wifi_ssid");
        nvs_erase_key(handle, "wifi_pass");
    }

    if (mode == 2 && ap_pass) {
        nvs_set_blob(handle, "ap_pass", ap_pass, strlen(ap_pass) + 1);
    } else {
        nvs_erase_key(handle, "ap_pass");
    }

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi настройки сохранены (mode=%d)", mode);
}

void WifiNvsStore::erase()
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Не удалось открыть NVS для стирания WiFi-ключей");
        return;
    }
    nvs_erase_key(handle, "wifi_mode");
    nvs_erase_key(handle, "wifi_ssid");
    nvs_erase_key(handle, "wifi_pass");
    nvs_erase_key(handle, "ap_pass");
    nvs_erase_key(handle, "sta_fail_cnt");
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi ключи стёрты из NVS");
}

uint8_t WifiNvsStore::sta_fail_count()
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) return 0;
    uint8_t count = 0;
    nvs_get_u8(handle, "sta_fail_cnt", &count);
    nvs_close(handle);
    return count;
}

void WifiNvsStore::set_sta_fail_count(uint8_t count)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, "sta_fail_cnt", count);
    nvs_commit(handle);
    nvs_close(handle);
}
