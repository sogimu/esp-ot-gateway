#include "infrastructure/driven/boiler_nvs_store.h"

#include "infrastructure/driven/nvs_config_store.h"       // NvsCalibBlob, NvsEfficiencyBlob
#include "infrastructure/driven/ota_validity_adapter.h"    // is_pending_global() — D9 freeze
#include "application/ports/driven/iheating_state_store.h"
#include "domain/value_objects/ch_mode.h"
#include "domain/value_objects/ch_schedule.h"  // PID_Schedule

#include "esp_log.h"
#include "nvs.h"

#include <cstring>

static const char* TAG = "boiler_nvs";

// D10: версия on-disk формата namespace "config". Текущий формат = v1.
static constexpr uint8_t CFG_VER = 1;

static bool frozen() {
    return OtaValidityAdapter::is_pending_global();
}

// ───────────────────────────────────────────────────────────────────────────

void BoilerNvsStore::load_boiler_config(IHeatingStateStore& s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return;

    int32_t i32; uint8_t u8; int16_t i16; size_t sz;

    // D10: версия формата — отсутствие ключа = v1
    uint8_t cfg_ver = CFG_VER;
    nvs_get_u8(h, "cfg_ver", &cfg_ver);
    ESP_LOGI(TAG, "cfg_ver=%u", (unsigned)cfg_ver);

    // CH/DHW enables
    if (nvs_get_u8(h, "ch_en", &u8) == ESP_OK)
        { s.lock_exclusive(); s.set_ch_enable(u8 != 0); s.unlock_exclusive(); }
    if (nvs_get_u8(h, "dhw_en", &u8) == ESP_OK)
        { s.lock_exclusive(); s.set_dhw_enable(u8 != 0); s.unlock_exclusive(); }

    // DHW hysteresis
    if (nvs_get_u8(h, "dhw_hyst", &u8) == ESP_OK) {
        float v = u8 / 10.0f; if (v >= 0.5f && v <= 10.0f)
        { s.lock_exclusive(); s.set_dhw_hysteresis(v); s.unlock_exclusive(); }
    }

    // Setpoints (validated)
    if (nvs_get_i16(h, "ch_sp", &i16) == ESP_OK && i16 >= 20 && i16 <= 80)
        { s.lock_exclusive(); s.set_ch_setpoint((float)i16); s.unlock_exclusive(); }
    if (nvs_get_i16(h, "dhw_sp", &i16) == ESP_OK && i16 >= 35 && i16 <= 80)
        { s.lock_exclusive(); s.set_dhw_setpoint((float)i16); s.unlock_exclusive(); }

    // CH mode
    if (nvs_get_i32(h, "ch_mode", &i32) == ESP_OK)
        { s.lock_exclusive(); s.set_ch_mode(static_cast<CHMode>(i32)); s.unlock_exclusive(); }

    // PID config
    float pid_kp=2.0f, pid_ki=0.01f, pid_kd=0.0f;
    int32_t pid_dt=60, pid_lockout=300;
    uint8_t pid_sns=0; int16_t pid_trg=220; uint8_t pid_hys_u8=5;
    if (nvs_get_i32(h, "pid_kp", &i32) == ESP_OK) pid_kp = i32 / 1000.0f;
    if (nvs_get_i32(h, "pid_ki", &i32) == ESP_OK) pid_ki = i32 / 100000.0f;
    if (nvs_get_i32(h, "pid_kd", &i32) == ESP_OK) pid_kd = i32 / 1000.0f;
    if (nvs_get_i32(h, "pid_dt", &pid_dt) != ESP_OK) pid_dt = 60;
    nvs_get_u8(h, "pid_sns", &pid_sns);
    nvs_get_i16(h, "pid_trg", &pid_trg);
    if (nvs_get_i32(h, "pid_lck", &pid_lockout) != ESP_OK) pid_lockout = 300;
    nvs_get_u8(h, "pid_hys", &pid_hys_u8);
    s.lock_exclusive();
    s.set_pid_config(pid_kp, pid_ki, pid_kd, (int)pid_dt, (int)pid_sns,
                     pid_trg / 10.0f, (int)pid_lockout);
    s.set_pid_hysteresis(pid_hys_u8 / 10.0f);

    // PID schedule — двухшаговый запрос размера (D10)
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

    // Boiler model config — calibration blob (with migration)
    {
        sz = 0;
        nvs_get_blob(h, "boiler", nullptr, &sz);
        if (sz == 40) {
            struct __attribute__((packed)) Legacy40 {
                float k_calib, p_max, gas_calorific, gas_temp_offset;
                float ch_pmin_warm, ch_pmax_warm, ch_pmin_hot, ch_pmax_hot;
                float dhw_pmin, dhw_pmax;
            } old_cal;
            size_t osz = sizeof(old_cal);
            if (nvs_get_blob(h, "boiler", &old_cal, &osz) == ESP_OK && osz == 40) {
                s.lock_exclusive();
                s.set_k_calib(old_cal.k_calib);
                s.set_p_max(old_cal.p_max);
                s.set_gas_calorific(old_cal.gas_calorific);
                s.set_gas_temp_offset(old_cal.gas_temp_offset);
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

    // Efficiency blob — двухшаговый запрос (D10)
    {
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

void BoilerNvsStore::save_boiler_config(const IHeatingStateStore& s)
{
    // D9: заморозка записи во время PENDING_VERIFY
    if (frozen()) return;

    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, "ch_en", s.is_ch_enabled() ? 1 : 0);
    nvs_set_u8(h, "dhw_en", s.is_dhw_enabled() ? 1 : 0);
    nvs_set_i16(h, "ch_sp", (int16_t)s.get_ch_setpoint());
    nvs_set_i16(h, "dhw_sp", (int16_t)s.get_dhw_setpoint());
    nvs_set_u8(h, "dhw_hyst", (uint8_t)(s.get_dhw_hysteresis() * 10.0f + 0.5f));
    nvs_set_i32(h, "ch_mode", static_cast<int32_t>(s.get_ch_mode()));

    // PID
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

    // Efficiency
    {
        NvsEfficiencyBlob eff;
        eff.t1 = s.get_eff_t1(); eff.v1 = s.get_eff_v1();
        eff.t2 = s.get_eff_t2(); eff.v2 = s.get_eff_v2();
        eff.t3 = s.get_eff_t3(); eff.v3 = s.get_eff_v3();
        nvs_set_blob(h, "eff", &eff, sizeof(eff));
    }

    nvs_set_u8(h, "cfg_ver", CFG_VER);

    nvs_commit(h); nvs_close(h);
}
