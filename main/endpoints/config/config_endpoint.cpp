#include "endpoints/config/config_endpoint.h"
#include "model/model.h"
#include "pid/pid_service.h"
#include "endpoints/sntp/sntp_endpoint.h"
#include "endpoints/opentherm/opentherm_endpoint.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <cstring>

// ===== init =====

void ConfigEndpoint::init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// ===== "config" namespace =====

void ConfigEndpoint::load_config(Model& model, PidService& pid,
                                  SntpEndpoint& sntp, OpenthermEndpoint& ot)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return;

    int32_t tz = 0;
    if (nvs_get_i32(h, "tz_offset", &tz) == ESP_OK) {
        model.set_tz_offset((int)tz);
        sntp.set_timezone((int)tz);
    }

    uint8_t u8;
    if (nvs_get_u8(h, "ch_en", &u8) == ESP_OK) {
        bool en = (u8 != 0);
        model.set_ch_enable(en);
        ot.set_ch_enable(en);
    }
    if (nvs_get_u8(h, "dhw_en", &u8) == ESP_OK) {
        bool en = (u8 != 0);
        model.set_dhw_enable(en);
        ot.set_dhw_enable(en);
    }

    if (nvs_get_u8(h, "dhw_hyst", &u8) == ESP_OK) {
        float hyst = (float)u8 / 10.0f;
        if (hyst >= 0.5f && hyst <= 10.0f) {
            model.set_dhw_hysteresis(hyst);
            ot.set_dhw_hysteresis(hyst);
        }
    }

    int16_t i16;
    if (nvs_get_i16(h, "ch_sp", &i16) == ESP_OK) {
        if (i16 >= 20 && i16 <= 80) {
            float sp = (float)i16;
            ot.set_ch_setpoint(sp);
            model.set_ch_setpoint(sp);
        }
    }
    if (nvs_get_i16(h, "dhw_sp", &i16) == ESP_OK) {
        if (i16 >= 35 && i16 <= 80) {
            float sp = (float)i16;
            ot.set_dhw_setpoint(sp);
            model.set_dhw_setpoint(sp);
        }
    }

    size_t sz = sizeof(CH_Schedule);
    CH_Schedule sched;
    if (nvs_get_blob(h, "schedule", &sched, &sz) == ESP_OK) {
        model.set_schedule(sched);
        if (sched.enabled && sched.temps[0] >= 20.0f && sched.temps[0] <= 80.0f) {
            float sp = sched.temps[0];
            ot.set_ch_setpoint(sp);
            model.set_ch_setpoint(sp);
        }
    }

    char srv[64];
    bool srv0_loaded = false, srv1_loaded = false;
    sz = sizeof(srv);
    if (nvs_get_blob(h, "sntp_srv0", srv, &sz) == ESP_OK) {
        model.set_sntp_server0(srv);
        srv0_loaded = true;
    }
    sz = sizeof(srv);
    if (nvs_get_blob(h, "sntp_srv1", srv, &sz) == ESP_OK) {
        model.set_sntp_server1(srv);
        srv1_loaded = true;
    }
    if (srv0_loaded || srv1_loaded) {
        sntp.set_servers(
            model.get_sntp_server0(),
            model.get_sntp_server1());
    }

    int32_t i32;
    float kp = 2.0f, ki = 0.01f, kd = 0.0f;
    int dt_sec = 60, sensor = 0, lockout = 300;
    float target = 22.0f;
    bool pid_en = false;

    if (nvs_get_u8(h, "pid_en", &u8) == ESP_OK) pid_en = (u8 != 0);
    if (nvs_get_i32(h, "pid_kp", &i32) == ESP_OK) kp = (float)i32 / 100.0f;
    if (nvs_get_i32(h, "pid_ki", &i32) == ESP_OK) ki = (float)i32 / 10000.0f;
    if (nvs_get_i32(h, "pid_kd", &i32) == ESP_OK) kd = (float)i32 / 100.0f;
    if (nvs_get_i32(h, "pid_dt", &i32) == ESP_OK) dt_sec = (int)i32;
    if (nvs_get_i32(h, "pid_sensor", &i32) == ESP_OK) sensor = (int)i32;
    if (nvs_get_i32(h, "pid_target", &i32) == ESP_OK) target = (float)i32 / 10.0f;
    if (nvs_get_i32(h, "pid_lockout", &i32) == ESP_OK) lockout = (int)i32;
    float hyst = 0.5f;
    if (nvs_get_i32(h, "pid_hyst", &i32) == ESP_OK) hyst = (float)i32 / 10.0f;
    int ch_mode = 0;
    if (nvs_get_i32(h, "ch_mode", &i32) == ESP_OK) ch_mode = (int)i32;

    model.set_ch_mode(ch_mode);
    pid.set_config(kp, ki, kd, dt_sec, sensor, target, lockout);
    pid.set_hysteresis(hyst);
    if (ch_mode == 1) {
        pid.set_enabled(true);
    } else {
        pid.set_enabled(pid_en);
    }

    nvs_close(h);
}

void ConfigEndpoint::save_config(const Model& model, const PidService& pid)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_i32(h, "tz_offset", (int32_t)model.get_tz_offset());
    nvs_set_u8(h, "ch_en", model.is_ch_enabled() ? 1 : 0);
    nvs_set_u8(h, "dhw_en", model.is_dhw_enabled() ? 1 : 0);
    nvs_set_i16(h, "ch_sp", (int16_t)model.get_ch_setpoint());
    nvs_set_i16(h, "dhw_sp", (int16_t)model.get_dhw_setpoint());
    nvs_set_blob(h, "schedule", (const void*)&model.get_schedule(), sizeof(CH_Schedule));

    nvs_set_u8(h, "dhw_hyst", (uint8_t)(model.get_dhw_hysteresis() * 10.0f + 0.5f));

    nvs_set_blob(h, "sntp_srv0", model.get_sntp_server0(), strlen(model.get_sntp_server0()) + 1);
    nvs_set_blob(h, "sntp_srv1", model.get_sntp_server1(), strlen(model.get_sntp_server1()) + 1);

    nvs_set_u8(h, "pid_en", pid.is_enabled() ? 1 : 0);
    nvs_set_i32(h, "pid_kp", (int32_t)(pid.get_kp() * 100.0f + 0.5f));
    nvs_set_i32(h, "pid_ki", (int32_t)(pid.get_ki() * 10000.0f + 0.5f));
    nvs_set_i32(h, "pid_kd", (int32_t)(pid.get_kd() * 100.0f + 0.5f));
    nvs_set_i32(h, "pid_dt", (int32_t)pid.get_dt_sec());
    nvs_set_i32(h, "pid_sensor", (int32_t)pid.get_room_sensor());
    nvs_set_i32(h, "pid_target", (int32_t)(pid.get_target_room() * 10.0f + 0.5f));
    nvs_set_i32(h, "pid_lockout", (int32_t)pid.get_lockout_sec());
    nvs_set_i32(h, "pid_hyst", (int32_t)(pid.get_hysteresis() * 10.0f + 0.5f));
    nvs_set_i32(h, "ch_mode", (int32_t)model.get_ch_mode());

    nvs_commit(h);
    nvs_close(h);
}

// ===== "stats" namespace =====

bool ConfigEndpoint::load_stats(uint32_t& burner_sec, float& integ_m3,
                                 NvsGasEmaBlob& ema, NvsHistBlob& hist,
                                 NvsCycleBlob& cycles, NvsCalibBlob& calib)
{
    nvs_handle_t h;
    if (nvs_open("stats", NVS_READONLY, &h) != ESP_OK) return false;

    uint32_t u32val = 0;
    if (nvs_get_u32(h, "burn_sec", &u32val) == ESP_OK) {
        burner_sec = u32val;
    }

    float fval = 0;
    size_t sz = sizeof(fval);
    if (nvs_get_blob(h, "integ_m3", &fval, &sz) == ESP_OK) {
        integ_m3 = fval;
    }

    sz = sizeof(ema);
    nvs_get_blob(h, "gas_ema", &ema, &sz);

    sz = sizeof(hist);
    nvs_get_blob(h, "hist", &hist, &sz);

    sz = sizeof(cycles);
    nvs_get_blob(h, "cycles", &cycles, &sz);

    sz = sizeof(calib);
    nvs_get_blob(h, "calib", &calib, &sz);

    nvs_close(h);
    return true;
}

void ConfigEndpoint::save_stats(uint32_t burner_sec, float integ_m3,
                                 const NvsGasEmaBlob& ema, const NvsHistBlob& hist,
                                 const NvsCycleBlob& cycles, const NvsCalibBlob& calib)
{
    nvs_handle_t h;
    if (nvs_open("stats", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u32(h, "burn_sec", burner_sec);

    nvs_set_blob(h, "integ_m3", &integ_m3, sizeof(integ_m3));

    nvs_set_blob(h, "gas_ema", &ema, sizeof(ema));

    nvs_set_blob(h, "hist", &hist, sizeof(hist));

    nvs_set_blob(h, "cycles", &cycles, sizeof(cycles));

    nvs_set_blob(h, "calib", &calib, sizeof(calib));

    nvs_commit(h);
    nvs_close(h);
}

// ===== "meter" namespace =====

bool ConfigEndpoint::load_meter(Model& model)
{
    nvs_handle_t h;
    if (nvs_open("meter", NVS_READONLY, &h) != ESP_OK) return false;

    NvsMeterBlob* blob = new NvsMeterBlob;
    size_t sz = sizeof(*blob);
    if (nvs_get_blob(h, "data", blob, &sz) == ESP_OK) {
        model.set_gas_meter_base(blob->base_reading);
        model.set_last_correction_refs(blob->last_correction_actual,
                                         blob->integral_at_last_correction);
        int n = blob->corrections_count;
        if (n > CORRECTION_LOG_SIZE) n = CORRECTION_LOG_SIZE;
        for (int i = 0; i < n; i++) {
            CorrectionEntry e;
            e.timestamp       = blob->corrections[i].timestamp;
            e.actual_reading  = blob->corrections[i].actual_reading;
            e.estimated_total = blob->corrections[i].estimated_total;
            e.difference      = blob->corrections[i].difference;
            e.prev_k_calib    = blob->corrections[i].prev_k_calib;
            e.new_k_calib     = blob->corrections[i].new_k_calib;
            model.add_correction(e);
        }
    }
    delete blob;
    nvs_close(h);
    return true;
}

void ConfigEndpoint::save_meter(const Model& model)
{
    nvs_handle_t h;
    if (nvs_open("meter", NVS_READWRITE, &h) != ESP_OK) return;

    NvsMeterBlob* blob = new NvsMeterBlob;
    memset(blob, 0, sizeof(*blob));
    blob->base_reading      = model.get_gas_meter_base();
    blob->last_correction_actual     = model.get_last_correction_actual();
    blob->integral_at_last_correction = model.get_integral_at_last_correction();
    blob->corrections_count = model.get_correction_count();
    blob->corrections_head  = 0;

    int total = model.get_correction_count();
    for (int i = 0; i < total && i < CORRECTION_LOG_SIZE; i++) {
        CorrectionEntry src;
        model.get_correction_by_index(i, src);
        blob->corrections[i].timestamp       = src.timestamp;
        blob->corrections[i].actual_reading  = src.actual_reading;
        blob->corrections[i].estimated_total = src.estimated_total;
        blob->corrections[i].difference      = src.difference;
        blob->corrections[i].prev_k_calib    = src.prev_k_calib;
        blob->corrections[i].new_k_calib     = src.new_k_calib;
    }

    nvs_set_blob(h, "data", blob, sizeof(*blob));
    nvs_commit(h);
    nvs_close(h);
    delete blob;
}

// ===== "predict" namespace =====

bool ConfigEndpoint::load_predict(float rates[3], int& idx, int& count)
{
    nvs_handle_t h;
    if (nvs_open("predict", NVS_READONLY, &h) != ESP_OK) return false;

    NvsPredictBlob blob;
    size_t sz = sizeof(blob);
    if (nvs_get_blob(h, "dhw_hist", &blob, &sz) == ESP_OK && sz == sizeof(blob)) {
        memcpy(rates, blob.rates, sizeof(float) * 3);
        idx   = (int)blob.idx;
        count = (int)blob.count;
    }

    nvs_close(h);
    return true;
}

void ConfigEndpoint::save_predict(const float rates[3], int idx, int count)
{
    nvs_handle_t h;
    if (nvs_open("predict", NVS_READWRITE, &h) != ESP_OK) return;

    NvsPredictBlob blob;
    memcpy(blob.rates, rates, sizeof(float) * 3);
    blob.idx   = (int32_t)idx;
    blob.count = (int32_t)count;

    nvs_set_blob(h, "dhw_hist", &blob, sizeof(blob));
    nvs_commit(h);
    nvs_close(h);
}
