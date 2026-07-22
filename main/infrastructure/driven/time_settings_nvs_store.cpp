#include "infrastructure/driven/time_settings_nvs_store.h"

#include "application/ports/driven/iheating_state_store.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_partition.h"

#include <cstring>

static const char* TAG = "time_nvs";

// ── init ───────────────────────────────────────────────────────────────────

void TimeSettingsNvsStore::init()
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // D10: во время PENDING_VERIFY НЕ стираем NVS. Стирание уничтожило бы
        // данные валидной (предыдущей) прошивки при возможном откате.
        // Состояние PENDING_VERIFY запрашиваем НАПРЯМУЮ из партиции — init()
        // вызывается до создания OtaValidityAdapter.
        bool pending = false;
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t st;
        if (running && esp_ota_get_state_partition(running, &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY) {
            pending = true;
        }
        if (pending) {
            ESP_LOGW(TAG, "nvs_flash_init err=0x%x, но образ на проверке "
                     "(PENDING_VERIFY) — nvs_flash_erase пропущен, повторный init", r);
            nvs_flash_init();
        } else {
            nvs_flash_erase();
            nvs_flash_init();
        }
    }
}

// ── "config" namespace (time only) ─────────────────────────────────────────

void TimeSettingsNvsStore::load_time_settings(IHeatingStateStore& s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return;

    int32_t i32; char buf[128]; size_t sz;

    if (nvs_get_i32(h, "tz_offset", &i32) == ESP_OK
        && i32 >= -12 && i32 <= 14) {
        s.lock_exclusive(); s.set_tz_offset((int)i32); s.unlock_exclusive();
    }

    buf[0] = '\0'; sz = 0;
    if (nvs_get_blob(h, "sntp_srv0", nullptr, &sz) == ESP_OK && sz <= sizeof(buf)) {
        nvs_get_blob(h, "sntp_srv0", buf, &sz);
        buf[sizeof(buf) - 1] = '\0';
        s.lock_exclusive(); s.set_sntp_server0(buf); s.unlock_exclusive();
    }
    buf[0] = '\0'; sz = 0;
    if (nvs_get_blob(h, "sntp_srv1", nullptr, &sz) == ESP_OK && sz <= sizeof(buf)) {
        nvs_get_blob(h, "sntp_srv1", buf, &sz);
        buf[sizeof(buf) - 1] = '\0';
        s.lock_exclusive(); s.set_sntp_server1(buf); s.unlock_exclusive();
    }

    nvs_close(h);
}

void TimeSettingsNvsStore::save_time_settings(const IHeatingStateStore& s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_i32(h, "tz_offset", (int32_t)s.get_tz_offset());
    const char* s0 = s.get_sntp_server0(); const char* s1 = s.get_sntp_server1();
    nvs_set_blob(h, "sntp_srv0", s0, strlen(s0)+1);
    nvs_set_blob(h, "sntp_srv1", s1, strlen(s1)+1);

    nvs_commit(h); nvs_close(h);
}
