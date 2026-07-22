#include "infrastructure/driven/mqtt_nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>

// D10: двухшаговое чтение строкового блоба. Сначала запрос размера через
// nvs_get_blob с NULL, затем чтение только если размер помещается в буфер.
// Возвращает ESP_OK только при успехе; гарантирует null-терминатор. Блоб чужого
// (большего) размера → буфер нетронут (вызывающий оставляет дефолты).
static esp_err_t mqtt_load_str_blob(nvs_handle_t h, const char* key,
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

void MqttNvsStore::init()
{
    nvs_flash_init();
}

// ── MQTT broker config ("config" namespace) ────────────────────

void MqttNvsStore::save_mqtt_config(const char* host, uint16_t port,
                                     const char* user, const char* pass,
                                     const char* prefix, bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_blob(h, "mqtt_host", host, strlen(host) + 1);
    nvs_set_u16(h, "mqtt_port", port);
    nvs_set_blob(h, "mqtt_user", user, strlen(user) + 1);
    nvs_set_blob(h, "mqtt_pass", pass, strlen(pass) + 1);
    nvs_set_blob(h, "mqtt_pref", prefix, strlen(prefix) + 1);
    nvs_set_u8(h, "mqtt_en", enabled ? 1 : 0);
    nvs_set_u8(h, "mqtt_tls", tls ? 1 : 0);
    nvs_set_u16(h, "mqtt_sti", status_interval_s);
    nvs_set_u16(h, "mqtt_ssi", stats_interval_s);

    nvs_commit(h); nvs_close(h);
}

bool MqttNvsStore::load_mqtt_config(char* host, size_t host_size,
                                     uint16_t& port,
                                     char* user, size_t user_size,
                                     char* pass, size_t pass_size,
                                     char* prefix, size_t prefix_size,
                                     bool& enabled, bool& tls)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t u8;

    // D10: двухшаговый запрос размера для каждого строкового блоба.
    mqtt_load_str_blob(h, "mqtt_host", host, host_size);

    if (nvs_get_u16(h, "mqtt_port", &port) != ESP_OK) {
        // оставить значение по умолчанию
    }

    mqtt_load_str_blob(h, "mqtt_user", user, user_size);
    mqtt_load_str_blob(h, "mqtt_pass", pass, pass_size);
    mqtt_load_str_blob(h, "mqtt_pref", prefix, prefix_size);

    if (nvs_get_u8(h, "mqtt_en", &u8) == ESP_OK)
        enabled = (u8 != 0);
    if (nvs_get_u8(h, "mqtt_tls", &u8) == ESP_OK)
        tls = (u8 != 0);

    nvs_close(h);
    return true;
}

void MqttNvsStore::save_mqtt_intervals(uint16_t status_s, uint16_t stats_s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u16(h, "mqtt_sti", status_s);
    nvs_set_u16(h, "mqtt_ssi", stats_s);
    nvs_commit(h); nvs_close(h);
}

bool MqttNvsStore::load_mqtt_intervals(uint16_t& status_s, uint16_t& stats_s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = false;
    uint16_t v;
    if (nvs_get_u16(h, "mqtt_sti", &v) == ESP_OK) { status_s = v; ok = true; }
    if (nvs_get_u16(h, "mqtt_ssi", &v) == ESP_OK) { stats_s = v; ok = true; }
    nvs_close(h);
    return ok;
}
