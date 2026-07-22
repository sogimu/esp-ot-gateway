#include "infrastructure/driven/nvs_config_store.h"
#include "application/ports/driven/iheating_state_store.h"
#include "domain/value_objects/ch_schedule.h"
#include "infrastructure/driven/ota_validity_adapter.h"  // is_pending_global() — NVS-заморозка (D9)
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"  // esp_ota_get_running_partition/state — прямая проверка PENDING_VERIFY в init()
#include <cstring>
#include <cstdlib>
#include <cassert>

static const char* NVS_TAG = "nvs_store";

// ── Заморозка NVS во время PENDING_VERIFY (D9) ───────────────
// Свежезалитая прошивка загружается в состоянии ESP_OTA_IMG_PENDING_VERIFY и
// должна за ~90 с доказать жизнеспособность. До подтверждения (mark_valid)
// НИКАКИЕ блобы не пишутся: если прошивка откатится, данные предыдущей
// (валидной) версии не будут затёрты новыми, возможно несовместимыми данными.
// Связь с validity-адаптером — infrastructure→infrastructure (один слой,
// циклической зависимости нет). is_pending_global() возвращает false, если
// экземпляр ещё не создан или образ уже подтверждён.
static bool nvs_write_frozen_during_verify()
{
    if (OtaValidityAdapter::is_pending_global()) {
        ESP_LOGD(NVS_TAG, "save блоба пропущен: образ на проверке (PENDING_VERIFY)");
        return true;
    }
    return false;
}

// D10: версии on-disk формата namespace'ов. Текущий формат = v1 (миграции в
// этом дропе нет — закладывается только готовность). cfg_ver/stats_ver пишутся
// в save_* (а они заморожены до mark_valid через is_pending_global(), поэтому
// bump ключей версий — только после подтверждения прошивки).
static constexpr uint8_t NVS_CONFIG_VER = 1;  // "config" namespace

// ── init ─────────────────────────────────────────────────────

void NvsConfigStore::init()
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // D10: во время PENDING_VERIFY НЕ стираем NVS. Стирание уничтожило бы
        // данные валидной (предыдущей) прошивки при возможном откате — свежая
        // прошивка ещё не подтверждена и может откатиться. Логируем и повторяем
        // nvs_flash_init без erase. Полное стирание оставлено только осознанному
        // factory-reset (/api/wifi/forget → factory_reset_and_reboot).
        //
        // Состояние PENDING_VERIFY запрашиваем НАПРЯМУЮ из партиции, а не через
        // OtaValidityAdapter::is_pending_global(): init() вызывается в main.cpp
        // ДО создания OtaValidityAdapter, и синглтон ещё не успевает инициализироваться.
        bool pending = false;
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t st;
        if (running && esp_ota_get_state_partition(running, &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY) {
            pending = true;
        }
        if (pending) {
            ESP_LOGW(NVS_TAG, "nvs_flash_init err=0x%x, но образ на проверке "
                     "(PENDING_VERIFY) — nvs_flash_erase пропущен, повторный init", r);
            nvs_flash_init();
        } else {
            nvs_flash_erase();
            nvs_flash_init();
        }
    }
}

// ── "config" namespace ───────────────────────────────────────

void NvsConfigStore::load_all(IHeatingStateStore& s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return;

    int32_t i32; uint8_t u8; int16_t i16; char buf[128]; size_t sz;

    // Time-related keys (tz_offset, sntp_srv*) — only non-boiler config.
    // Boiler keys (CH/DHW/PID/calibration) moved to BoilerNvsStore (D14).

    if (nvs_get_i32(h, "tz_offset", &i32) == ESP_OK
        && i32 >= -12 && i32 <= 14) {
        s.lock_exclusive(); s.set_tz_offset((int)i32); s.unlock_exclusive(); }

    // D10: двухшаговый запрос размера для строковых блобов sntp_srv*
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

void NvsConfigStore::save_config(const IHeatingStateStore& s)
{
    // Non-boiler keys: tz_offset + sntp_srv* (boiler keys → BoilerNvsStore).
    // D9: заморозка блобов sntp_srv* во время PENDING_VERIFY.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_i32(h, "tz_offset", (int32_t)s.get_tz_offset());
    const char* s0 = s.get_sntp_server0(); const char* s1 = s.get_sntp_server1();
    nvs_set_blob(h, "sntp_srv0", s0, strlen(s0)+1);
    nvs_set_blob(h, "sntp_srv1", s1, strlen(s1)+1);

    nvs_commit(h); nvs_close(h);
}

bool NvsConfigStore::save_eff(const IHeatingStateStore& state)
{
    // D9: во время PENDING_VERIFY блоб eff не пишем.
    if (nvs_write_frozen_during_verify()) return false;

    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return false;
    NvsEfficiencyBlob eff;
    eff.t1 = state.get_eff_t1(); eff.v1 = state.get_eff_v1();
    eff.t2 = state.get_eff_t2(); eff.v2 = state.get_eff_v2();
    eff.t3 = state.get_eff_t3(); eff.v3 = state.get_eff_v3();
    esp_err_t r = nvs_set_blob(n, "eff", &eff, sizeof(eff));
    nvs_commit(n);
    nvs_close(n);
    return r == ESP_OK;
}

bool NvsConfigStore::load_eff(IHeatingStateStore& state)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    // D10: двухшаговый запрос размера eff-блоба. Чужой размер → дефолты.
    NvsEfficiencyBlob eff;
    memset(&eff, 0, sizeof(eff));
    size_t sz = 0;
    bool ok = false;
    if (nvs_get_blob(n, "eff", nullptr, &sz) == ESP_OK && sz == sizeof(eff)) {
        if (nvs_get_blob(n, "eff", &eff, &sz) == ESP_OK) {
            state.lock_exclusive();
            state.set_eff_t1(eff.t1); state.set_eff_v1(eff.v1);
            state.set_eff_t2(eff.t2); state.set_eff_v2(eff.v2);
            state.set_eff_t3(eff.t3); state.set_eff_v3(eff.v3);
            state.unlock_exclusive();
            ok = true;
        }
    }
    nvs_close(n);
    return ok;
}
