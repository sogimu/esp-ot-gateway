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
static constexpr uint8_t NVS_STATS_VER  = 1;  // "stats"  namespace

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

    // D10: версия формата "config". Отсутствие ключа = v1 (текущий on-disk).
    uint8_t cfg_ver = NVS_CONFIG_VER;
    nvs_get_u8(h, "cfg_ver", &cfg_ver);
    ESP_LOGI(NVS_TAG, "config cfg_ver=%u", (unsigned)cfg_ver);

    if (nvs_get_i32(h, "tz_offset", &i32) == ESP_OK
        && i32 >= -12 && i32 <= 14) {
        s.lock_exclusive(); s.set_tz_offset((int)i32); s.unlock_exclusive(); }
    if (nvs_get_u8(h, "ch_en", &u8) == ESP_OK)
        { s.lock_exclusive(); s.set_ch_enable(u8 != 0); s.unlock_exclusive(); }
    if (nvs_get_u8(h, "dhw_en", &u8) == ESP_OK)
        { s.lock_exclusive(); s.set_dhw_enable(u8 != 0); s.unlock_exclusive(); }
    if (nvs_get_u8(h, "dhw_hyst", &u8) == ESP_OK) {
        float v = u8 / 10.0f; if (v >= 0.5f && v <= 10.0f)
        { s.lock_exclusive(); s.set_dhw_hysteresis(v); s.unlock_exclusive(); }
    }
    if (nvs_get_i16(h, "ch_sp", &i16) == ESP_OK && i16 >= 20 && i16 <= 80)
        { s.lock_exclusive(); s.set_ch_setpoint((float)i16); s.unlock_exclusive(); }
    if (nvs_get_i16(h, "dhw_sp", &i16) == ESP_OK && i16 >= 35 && i16 <= 80)
        { s.lock_exclusive(); s.set_dhw_setpoint((float)i16); s.unlock_exclusive(); }
    if (nvs_get_i32(h, "ch_mode", &i32) == ESP_OK)
        { s.lock_exclusive(); s.set_ch_mode(static_cast<CHMode>(i32)); s.unlock_exclusive(); }

    // D10: двухшаговый запрос размера для строковых блобов sntp_srv*. Сначала
    // узнаём размер, затем читаем только если он помещается в буфер. Так блоб
    // чужого (большего) размера не даёт INVALID_LENGTH и не оставляет мусор.
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

    // PID config — use dedicated variables to avoid field cross-contamination
    float pid_kp=2.0f, pid_ki=0.01f, pid_kd=0.0f;
    int32_t pid_dt=60, pid_lockout=300;
    uint8_t pid_sns=0; int16_t pid_trg=220; uint8_t pid_hys_u8=5;
    if (nvs_get_i32(h, "pid_kp", &i32) == ESP_OK) pid_kp = i32 / 1000.0f;
    if (nvs_get_i32(h, "pid_ki", &i32) == ESP_OK) pid_ki = i32 / 100000.0f;
    if (nvs_get_i32(h, "pid_kd", &i32) == ESP_OK) pid_kd = i32 / 1000.0f;
    if (nvs_get_i32(h, "pid_dt", &pid_dt) != ESP_OK) pid_dt = 60;
    nvs_get_u8(h, "pid_sns", &pid_sns);          // 0=T1, 1=T2
    nvs_get_i16(h, "pid_trg", &pid_trg);          // target_room * 10
    if (nvs_get_i32(h, "pid_lck", &pid_lockout) != ESP_OK) pid_lockout = 300;
    nvs_get_u8(h, "pid_hys", &pid_hys_u8);        // hysteresis * 10
    s.lock_exclusive();
    s.set_pid_config(pid_kp, pid_ki, pid_kd, (int)pid_dt, (int)pid_sns,
                     pid_trg / 10.0f, (int)pid_lockout);
    s.set_pid_hysteresis(pid_hys_u8 / 10.0f);
    // PID enable/disable is handled by PidPollInteractor constructor
    // D10: Load PID schedule — двухшаговый запрос, чтение только при точном
    // совпадении sizeof(PID_Schedule). Чужой размер → дефолты, без memcpy.
    {
        PID_Schedule ps;
        memset(&ps, 0, sizeof(ps));
        sz = 0;
        if (nvs_get_blob(h, "pid_sched", nullptr, &sz) == ESP_OK && sz == sizeof(ps)) {
            if (nvs_get_blob(h, "pid_sched", &ps, &sz) == ESP_OK) {
                s.set_pid_schedule(&ps);
            }
        }
    }
    s.unlock_exclusive();

    // Boiler model config — gas_temp_offset, CH/DHW power params, efficiency points
    {
        // Query actual blob size first (for format migration)
        sz = 0;
        nvs_get_blob(h, "boiler", nullptr, &sz);
        if (sz == 40) {
            // Migration from old format: 4 CH params (warm/hot) → 2 (input)
            struct __attribute__((packed)) NvsCalibBlob40 {
                float k_calib, p_max, gas_calorific;
                float gas_temp_offset;
                float ch_pmin_warm, ch_pmax_warm;
                float ch_pmin_hot,  ch_pmax_hot;
                float dhw_pmin, dhw_pmax;
            } old_cal;
            size_t osz = sizeof(old_cal);
            if (nvs_get_blob(h, "boiler", &old_cal, &osz) == ESP_OK && osz == 40) {
                s.lock_exclusive();
                // Keep: k_calib, p_max, gas_calorific, gas_temp_offset (dimensionless/scalar — still valid)
                s.set_k_calib(old_cal.k_calib);
                s.set_p_max(old_cal.p_max);
                s.set_gas_calorific(old_cal.gas_calorific);
                s.set_gas_temp_offset(old_cal.gas_temp_offset);
                // CH/DHW power: discard old output-power values, use new input-power defaults
                // (old model stored output kW; new model needs nameplate input kW)
                s.unlock_exclusive();
            }
        } else if (sz == sizeof(NvsCalibBlob)) {
            NvsCalibBlob cal;
            size_t nsz = sizeof(cal);
            if (nvs_get_blob(h, "boiler", &cal, &nsz) == ESP_OK) {
                s.lock_exclusive();
                s.set_gas_temp_offset(cal.gas_temp_offset);
                s.set_ch_pmin(cal.ch_pmin);
                s.set_ch_pmax(cal.ch_pmax);
                s.set_dhw_pmin(cal.dhw_pmin);
                s.set_dhw_pmax(cal.dhw_pmax);
                s.set_k_calib(cal.k_calib);
                s.set_p_max(cal.p_max);
                s.set_gas_calorific(cal.gas_calorific);
                s.unlock_exclusive();
            }
        }
    }
    {
        // D10: двухшаговый запрос размера eff-блоба. Чужой размер → дефолты.
        NvsEfficiencyBlob eff;
        memset(&eff, 0, sizeof(eff));
        sz = 0;
        if (nvs_get_blob(h, "eff", nullptr, &sz) == ESP_OK && sz == sizeof(eff)) {
            if (nvs_get_blob(h, "eff", &eff, &sz) == ESP_OK) {
                s.lock_exclusive();
                s.set_eff_t1(eff.t1); s.set_eff_v1(eff.v1);
                s.set_eff_t2(eff.t2); s.set_eff_v2(eff.v2);
                s.set_eff_t3(eff.t3); s.set_eff_v3(eff.v3);
                s.unlock_exclusive();
            }
        }
    }

    nvs_close(h);
}

void NvsConfigStore::save_config(const IHeatingStateStore& s)
{
    // D9: во время PENDING_VERIFY блобы (sntp_srv*, pid_sched, boiler, eff) не пишем.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_i32(h, "tz_offset", (int32_t)s.get_tz_offset());
    nvs_set_u8(h, "ch_en", s.is_ch_enabled() ? 1 : 0);
    nvs_set_u8(h, "dhw_en", s.is_dhw_enabled() ? 1 : 0);
    nvs_set_i16(h, "ch_sp", (int16_t)s.get_ch_setpoint());
    nvs_set_i16(h, "dhw_sp", (int16_t)s.get_dhw_setpoint());
    nvs_set_u8(h, "dhw_hyst", (uint8_t)(s.get_dhw_hysteresis() * 10.0f + 0.5f));
    nvs_set_i32(h, "ch_mode", static_cast<int32_t>(s.get_ch_mode()));
    const char* s0 = s.get_sntp_server0(); const char* s1 = s.get_sntp_server1();
    nvs_set_blob(h, "sntp_srv0", s0, strlen(s0)+1);
    nvs_set_blob(h, "sntp_srv1", s1, strlen(s1)+1);
    // PID config
    nvs_set_i32(h, "pid_kp", (int32_t)(s.get_pid_kp() * 1000.0f + 0.5f));
    nvs_set_i32(h, "pid_ki", (int32_t)(s.get_pid_ki() * 100000.0f + 0.5f));
    nvs_set_i32(h, "pid_kd", (int32_t)(s.get_pid_kd() * 1000.0f + 0.5f));
    nvs_set_i32(h, "pid_dt", (int32_t)s.get_pid_dt_sec());
    nvs_set_u8(h,  "pid_sns", (uint8_t)s.get_pid_room_sensor());
    nvs_set_i16(h, "pid_trg", (int16_t)(s.get_pid_target_room() * 10.0f + 0.5f));
    nvs_set_i32(h, "pid_lck", (int32_t)s.get_pid_lockout_sec());
    nvs_set_u8(h,  "pid_hys", (uint8_t)(s.get_pid_hysteresis() * 10.0f + 0.5f));
    // PID schedule
    {
        PID_Schedule ps;
        s.get_pid_schedule(&ps);
        nvs_set_blob(h, "pid_sched", &ps, sizeof(ps));
    }
    // Boiler model config
    {
        NvsCalibBlob cal;
        cal.k_calib = s.get_k_calib();
        cal.p_max = s.get_p_max();
        cal.gas_calorific = s.get_gas_calorific();
        cal.gas_temp_offset = s.get_gas_temp_offset();
        cal.ch_pmin = s.get_ch_pmin();
        cal.ch_pmax = s.get_ch_pmax();
        cal.dhw_pmin = s.get_dhw_pmin();
        cal.dhw_pmax = s.get_dhw_pmax();
        nvs_set_blob(h, "boiler", &cal, sizeof(cal));
    }
    {
        NvsEfficiencyBlob eff;
        eff.t1 = s.get_eff_t1(); eff.v1 = s.get_eff_v1();
        eff.t2 = s.get_eff_t2(); eff.v2 = s.get_eff_v2();
        eff.t3 = s.get_eff_t3(); eff.v3 = s.get_eff_v3();
        nvs_set_blob(h, "eff", &eff, sizeof(eff));
    }
    // D10: версия формата "config" (bump — только после mark_valid: save_config
    // заморожен до подтверждения через is_pending_global()).
    nvs_set_u8(h, "cfg_ver", NVS_CONFIG_VER);

    nvs_commit(h); nvs_close(h);
}


// ── "stats" namespace ────────────────────────────────────────

bool NvsConfigStore::load_stats(uint32_t& bs, float& im3,
                                   void* h, void* c, void* e, void* cal)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    uint32_t u32=0; if (nvs_get_u32(n, "burn_sec", &u32)==ESP_OK) bs=u32;

    // D10: версия формата "stats". Отсутствие ключа = v1 (текущий on-disk).
    uint8_t stats_ver = NVS_STATS_VER;
    nvs_get_u8(n, "stats_ver", &stats_ver);
    ESP_LOGI(NVS_TAG, "stats stats_ver=%u", (unsigned)stats_ver);

    // D10: двухшаговый запрос размера для каждого блоба. Сначала узнаём размер
    // (nvs_get_blob с NULL), читаем данные только при точном совпадении с
    // ожидаемым размером структуры. Чужой размер → буфер нетронут (дефолты),
    // никакого memcpy размера, взятого с диска.
    size_t sz = 0;
    float fv = 0;
    if (nvs_get_blob(n, "integ_m3", nullptr, &sz) == ESP_OK && sz == sizeof(fv)) {
        nvs_get_blob(n, "integ_m3", &fv, &sz);
        im3 = fv;
    }
    if (e) {
        sz = 0;
        if (nvs_get_blob(n, "gas_ema", nullptr, &sz) == ESP_OK && sz == sizeof(NvsGasEmaBlob)) {
            nvs_get_blob(n, "gas_ema", e, &sz);
        }
        // иначе: чужой размер/отсутствие — буфер e нетронут (вызывающий обнулил)
    }
    if (h) {
        // D10: блоб чужого размера (другое HIST_BINS или повреждённый) → буфер
        // h нетронут (вызывающий обнулил). Следующий периодический save
        // перепишет "hist" в текущем формате.
        sz = 0;
        if (nvs_get_blob(n, "hist", nullptr, &sz) == ESP_OK && sz == sizeof(NvsHistBlob)) {
            nvs_get_blob(n, "hist", h, &sz);
        }
    }
    if (c) {
        sz = 0;
        if (nvs_get_blob(n, "cycles", nullptr, &sz) == ESP_OK && sz == sizeof(NvsCycleBlob)) {
            nvs_get_blob(n, "cycles", c, &sz);
        }
    }
    if (cal) {
        sz = 0;
        if (nvs_get_blob(n, "calib", nullptr, &sz) == ESP_OK) {
            if (sz == 12) {
                // Migration from old 3-float blob — read old fields; new fields remain zero (caller fills defaults)
                nvs_get_blob(n, "calib", cal, &sz);
            } else if (sz == 40) {
                // Migration from old 4-CH-param blob — map to new 2-CH-param
                struct __attribute__((packed)) { float k,p,g,gt,cpw1,cpw2,cph1,cph2,dp1,dp2; } old;
                size_t osz = sizeof(old);
                if (nvs_get_blob(n, "calib", &old, &osz) == ESP_OK && osz == 40) {
                    auto* c = static_cast<NvsCalibBlob*>(cal);
                    // Keep scalar fields; leave CH/DHW at zero (caller fills defaults)
                    c->k_calib = old.k; c->p_max = old.p; c->gas_calorific = old.g;
                    c->gas_temp_offset = old.gt;
                    // ch_pmin, ch_pmax, dhw_pmin, dhw_pmax stay at zero (old output-power values incompatible)
                }
            } else if (sz == sizeof(NvsCalibBlob)) {
                nvs_get_blob(n, "calib", cal, &sz);
            }
        }
    }
    nvs_close(n); return true;
}

void NvsConfigStore::save_stats(const IHeatingStateStore&,
                                   uint32_t bs, float im3,
                                   const void* h, const void* c,
                                   const void* e, const void* cal)
{
    // D9: во время PENDING_VERIFY блобы (integ_m3, gas_ema, hist, cycles, calib) не пишем.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_u32(n, "burn_sec", bs);
    nvs_set_blob(n, "integ_m3", &im3, sizeof(im3));
    if (e) {
        assert(sizeof(NvsGasEmaBlob) == 28); // 5 floats + uint64_t — must match on-disk blob size
        nvs_set_blob(n, "gas_ema", e, sizeof(NvsGasEmaBlob));
    }
    if (h) {
        assert(sizeof(NvsHistBlob) == 4 + HIST_BINS * 4);
        nvs_set_blob(n, "hist", h, sizeof(NvsHistBlob));
    }
    if (c) {
        assert(sizeof(NvsCycleBlob) == 1040); // 4+4+4+4 + 256*2 + 256*2
        nvs_set_blob(n, "cycles", c, sizeof(NvsCycleBlob));
    }
    if (cal) {
        assert(sizeof(NvsCalibBlob) == 32); // 8 floats
        nvs_set_blob(n, "calib", cal, sizeof(NvsCalibBlob));
    }
    // D10: версия формата "stats" (bump — только после mark_valid: save_stats
    // заморожен до подтверждения через is_pending_global()).
    nvs_set_u8(n, "stats_ver", NVS_STATS_VER);
    nvs_commit(n); nvs_close(n);
}

// ── "meter" namespace ────────────────────────────────────────

bool NvsConfigStore::load_meter(IHeatingStateStore& s, void* blob)
{
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READONLY, &n) != ESP_OK) return false;
    NvsMeterBlob b;
    memset(&b, 0, sizeof(b));
    bool ok = false;

    // D10: двухшаговый запрос размера. Сначала узнаём размер блоба data, затем
    // ветвим: текущий формат (sizeof(NvsMeterBlob)) или legacy 32-записей.
    // Чужой/неизвестный размер → дефолты (ok=false), без memcpy размера с диска.
    size_t sz = 0;
    if (nvs_get_blob(n, "data", nullptr, &sz) == ESP_OK) {
        if (sz == sizeof(b)) {
            // Текущий on-disk формат — читаем напрямую.
            nvs_get_blob(n, "data", &b, &sz);
            ok = true;
        } else {
            // Legacy struct matching the old on-disk format with 32 entries
            // (CORRECTION_LOG_SIZE было 32 раньше). Проверяем размер точно.
            struct NvsMeterBlob32 {
                float base_reading, last_correction_actual, integral_at_last_correction;
                int32_t corrections_head, corrections_count;
                NvsCorrLogEntry corrections[32];
            } old;
            if (sz == sizeof(old)) {
                memset(&old, 0, sizeof(old));
                nvs_get_blob(n, "data", &old, &sz);
                b.base_reading = old.base_reading;
                b.last_correction_actual = old.last_correction_actual;
                b.integral_at_last_correction = old.integral_at_last_correction;
                int keep = old.corrections_count < 10 ? old.corrections_count : 10;
                b.corrections_count = keep;
                if (keep > 0) {
                    int old_head = old.corrections_head;
                    // Copy newest `keep` entries in order (oldest first)
                    for (int i = 0; i < keep; i++) {
                        int src = (old_head - keep + i + 32) % 32;
                        b.corrections[i] = old.corrections[src];
                    }
                    b.corrections_head = keep % 10;
                }
                ok = true;
            }
            // иначе: неизвестный размер — ok остаётся false, дефолты
        }
    }

    nvs_close(n);
    if (ok) {
        s.lock_exclusive();
        s.set_gas_meter_base(b.base_reading);
        s.unlock_exclusive();
        if (blob) memcpy(blob, &b, sizeof(NvsMeterBlob));
    }
    return ok;
}

void NvsConfigStore::save_meter(const IHeatingStateStore& s, const void* blob)
{
    // D9: во время PENDING_VERIFY блоб meter/data не пишем.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t n;
    if (nvs_open("meter", NVS_READWRITE, &n) != ESP_OK) return;
    NvsMeterBlob b;
    if (blob) {
        memcpy(&b, blob, sizeof(NvsMeterBlob));
        // Ensure base_reading in blob matches state (state is authoritative)
        b.base_reading = s.get_gas_meter_base();
    } else {
        memset(&b, 0, sizeof(b));
        b.base_reading = s.get_gas_meter_base();
    }
    nvs_set_blob(n, "data", &b, sizeof(b));
    nvs_commit(n); nvs_close(n);
}

// ── "predict" namespace ──────────────────────────────────────

bool NvsConfigStore::load_predict(float r[3], int& idx, int& cnt)
{
    nvs_handle_t n;
    if (nvs_open("predict", NVS_READONLY, &n) != ESP_OK) return false;
    // D10: двухшаговый запрос размера — чтение только при точном совпадении.
    NvsPredictBlob b; memset(&b, 0, sizeof(b));
    size_t sz = 0;
    if (nvs_get_blob(n, "dhw_hist", nullptr, &sz) == ESP_OK && sz == sizeof(b)) {
        if (nvs_get_blob(n, "dhw_hist", &b, &sz) == ESP_OK) {
            memcpy(r, b.rates, 3*sizeof(float)); idx=(int)b.idx; cnt=(int)b.count;
        }
    }
    nvs_close(n); return true;
}

void NvsConfigStore::save_predict(const float r[3], int idx, int cnt)
{
    // D9: во время PENDING_VERIFY блоб predict/dhw_hist не пишем.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t n;
    if (nvs_open("predict", NVS_READWRITE, &n) != ESP_OK) return;
    NvsPredictBlob b;
    memcpy(b.rates, r, 3*sizeof(float)); b.idx=(int32_t)idx; b.count=(int32_t)cnt;
    nvs_set_blob(n, "dhw_hist", &b, sizeof(b));
    nvs_commit(n); nvs_close(n);
}

// ── Burner stats persistence (stats namespace) ──

bool NvsConfigStore::load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
                                        uint32_t& inter_pause_sec, uint32_t& inter_cnt,
                                        uint32_t& mod_pause_sec, uint32_t& mod_cnt)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    bool ok = false;
    uint32_t v;
    if (nvs_get_u32(n, "burn_sec", &v) == ESP_OK) { burner_sec = v; ok = true; }
    if (nvs_get_u32(n, "pause_sec", &v) == ESP_OK) { total_pause_sec = v; ok = true; }
    if (nvs_get_u32(n, "cycle_cnt", &v) == ESP_OK) { cycle_cnt = v; ok = true; }
    if (nvs_get_u32(n, "inter_ps", &v) == ESP_OK) { inter_pause_sec = v; ok = true; }
    if (nvs_get_u32(n, "inter_cn", &v) == ESP_OK) { inter_cnt = v; ok = true; }
    if (nvs_get_u32(n, "mod_ps", &v) == ESP_OK) { mod_pause_sec = v; ok = true; }
    if (nvs_get_u32(n, "mod_cn", &v) == ESP_OK) { mod_cnt = v; ok = true; }
    nvs_close(n);
    return ok;
}

void NvsConfigStore::save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
                                        uint32_t inter_pause_sec, uint32_t inter_cnt,
                                        uint32_t mod_pause_sec, uint32_t mod_cnt)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_u32(n, "burn_sec", burner_sec);
    nvs_set_u32(n, "pause_sec", total_pause_sec);
    nvs_set_u32(n, "cycle_cnt", cycle_cnt);
    nvs_set_u32(n, "inter_ps", inter_pause_sec);
    nvs_set_u32(n, "inter_cn", inter_cnt);
    nvs_set_u32(n, "mod_ps", mod_pause_sec);
    nvs_set_u32(n, "mod_cn", mod_cnt);
    nvs_commit(n);
    nvs_close(n);
}

// ── Total uptime persistence (stats namespace, "uptime" key) ──

bool NvsConfigStore::load_total_uptime(uint32_t& total_uptime_sec)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    uint32_t v = 0;
    bool ok = (nvs_get_u32(n, "uptime", &v) == ESP_OK);
    if (ok) total_uptime_sec = v;
    nvs_close(n);
    return ok;
}

void NvsConfigStore::save_total_uptime(uint32_t total_uptime_sec)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_u32(n, "uptime", total_uptime_sec);
    nvs_commit(n);
    nvs_close(n);
}

void NvsConfigStore::save_integral(float value)
{
    // D9: во время PENDING_VERIFY блоб integ_m3 не пишем.
    if (nvs_write_frozen_during_verify()) return;

    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_blob(n, "integ_m3", &value, sizeof(value));
    nvs_commit(n);
    nvs_close(n);
}

// ── Efficiency blob (stats namespace, "eff" key) ──────────────

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
