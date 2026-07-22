#include "infrastructure/driven/heating_stats_nvs_store.h"

#include "infrastructure/driven/nvs_config_store.h"       // NvsHistBlob, NvsCycleBlob, etc.
#include "infrastructure/driven/ota_validity_adapter.h"    // is_pending_global() — D9 freeze
#include "application/ports/driven/iheating_state_store.h"

#include "esp_log.h"
#include "nvs.h"

#include <cstring>
#include <cassert>

static const char* TAG = "heat_nvs";
static constexpr uint8_t STATS_VER = 1;

static bool frozen() { return OtaValidityAdapter::is_pending_global(); }

// ── "stats" namespace ──────────────────────────────────────────────────────

bool HeatingStatsNvsStore::load_stats(uint32_t& bs, float& im3,
                                       void* h, void* c, void* e, void* cal)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    uint32_t u32=0; if (nvs_get_u32(n, "burn_sec", &u32)==ESP_OK) bs=u32;

    uint8_t stats_ver = STATS_VER;
    nvs_get_u8(n, "stats_ver", &stats_ver);
    ESP_LOGI(TAG, "stats_ver=%u", (unsigned)stats_ver);

    size_t sz = 0;
    float fv = 0;
    if (nvs_get_blob(n, "integ_m3", nullptr, &sz) == ESP_OK && sz == sizeof(fv)) {
        nvs_get_blob(n, "integ_m3", &fv, &sz); im3 = fv;
    }
    if (e) {
        sz = 0;
        if (nvs_get_blob(n, "gas_ema", nullptr, &sz) == ESP_OK && sz == sizeof(NvsGasEmaBlob))
            nvs_get_blob(n, "gas_ema", e, &sz);
    }
    if (h) {
        sz = 0;
        if (nvs_get_blob(n, "hist", nullptr, &sz) == ESP_OK && sz == sizeof(NvsHistBlob))
            nvs_get_blob(n, "hist", h, &sz);
    }
    if (c) {
        sz = 0;
        if (nvs_get_blob(n, "cycles", nullptr, &sz) == ESP_OK && sz == sizeof(NvsCycleBlob))
            nvs_get_blob(n, "cycles", c, &sz);
    }
    if (cal) {
        sz = 0;
        if (nvs_get_blob(n, "calib", nullptr, &sz) == ESP_OK) {
            if (sz == 12) {
                nvs_get_blob(n, "calib", cal, &sz);
            } else if (sz == 40) {
                struct __attribute__((packed)) { float k,p,g,gt,cpw1,cpw2,cph1,cph2,dp1,dp2; } old;
                size_t osz = sizeof(old);
                if (nvs_get_blob(n, "calib", &old, &osz) == ESP_OK && osz == 40) {
                    auto* cc = static_cast<NvsCalibBlob*>(cal);
                    cc->k_calib = old.k; cc->p_max = old.p; cc->gas_calorific = old.g;
                    cc->gas_temp_offset = old.gt;
                }
            } else if (sz == sizeof(NvsCalibBlob)) {
                nvs_get_blob(n, "calib", cal, &sz);
            }
        }
    }
    nvs_close(n); return true;
}

void HeatingStatsNvsStore::save_stats(const IHeatingStateStore&,
                                       uint32_t bs, float im3,
                                       const void* h, const void* c,
                                       const void* e, const void* cal)
{
    if (frozen()) return;
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_u32(n, "burn_sec", bs);
    nvs_set_blob(n, "integ_m3", &im3, sizeof(im3));
    if (e) { assert(sizeof(NvsGasEmaBlob)==28); nvs_set_blob(n, "gas_ema", e, sizeof(NvsGasEmaBlob)); }
    if (h) { assert(sizeof(NvsHistBlob)==4+HIST_BINS*4); nvs_set_blob(n, "hist", h, sizeof(NvsHistBlob)); }
    if (c) { assert(sizeof(NvsCycleBlob)==1040); nvs_set_blob(n, "cycles", c, sizeof(NvsCycleBlob)); }
    if (cal) { assert(sizeof(NvsCalibBlob)==32); nvs_set_blob(n, "calib", cal, sizeof(NvsCalibBlob)); }
    nvs_set_u8(n, "stats_ver", STATS_VER);
    nvs_commit(n); nvs_close(n);
}

// ── uptime (stats namespace) ──────────────────────────────────────────────

bool HeatingStatsNvsStore::load_total_uptime(uint32_t& v)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READONLY, &n) != ESP_OK) return false;
    uint32_t u32 = 0; bool ok = (nvs_get_u32(n, "uptime", &u32) == ESP_OK);
    nvs_close(n);
    if (ok) v = u32;
    return ok;
}

void HeatingStatsNvsStore::save_total_uptime(uint32_t v)
{
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_u32(n, "uptime", v);
    nvs_commit(n); nvs_close(n);
}

// ── integral_m3 (stats namespace) ─────────────────────────────────────────

void HeatingStatsNvsStore::save_integral(float v)
{
    if (frozen()) return;
    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_blob(n, "integ_m3", &v, sizeof(v));
    nvs_commit(n); nvs_close(n);
}

// ── "meter" namespace ─────────────────────────────────────────────────────

bool HeatingStatsNvsStore::load_meter(IHeatingStateStore& s, void* blob)
{
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READONLY, &n) != ESP_OK) return false;
    NvsMeterBlob b; memset(&b, 0, sizeof(b));
    bool ok = false; size_t sz = 0;
    if (nvs_get_blob(n, "data", nullptr, &sz) == ESP_OK) {
        if (sz == sizeof(b)) { nvs_get_blob(n, "data", &b, &sz); ok = true; }
        else {
            struct NvsMeterBlob32 { float br,lca,ilc; int32_t ch,cc; NvsCorrLogEntry c[32]; } old;
            if (sz == sizeof(old)) {
                memset(&old, 0, sizeof(old)); nvs_get_blob(n, "data", &old, &sz);
                b.base_reading = old.br; b.last_correction_actual = old.lca; b.integral_at_last_correction = old.ilc;
                int keep = old.cc < 10 ? old.cc : 10; b.corrections_count = keep;
                if (keep > 0) { int oh = old.ch; for (int i=0;i<keep;i++) { int src=(oh-keep+i+32)%32; b.corrections[i]=old.c[src]; } b.corrections_head=keep%10; }
                ok = true;
            }
        }
    }
    nvs_close(n);
    if (ok) { s.lock_exclusive(); s.set_gas_meter_base(b.base_reading); s.unlock_exclusive(); if (blob) memcpy(blob, &b, sizeof(NvsMeterBlob)); }
    return ok;
}

void HeatingStatsNvsStore::save_meter(const IHeatingStateStore& s, const void* blob)
{
    if (frozen()) return;
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READWRITE, &n) != ESP_OK) return;
    NvsMeterBlob b;
    if (blob) { memcpy(&b, blob, sizeof(NvsMeterBlob)); b.base_reading = s.get_gas_meter_base(); }
    else { memset(&b, 0, sizeof(b)); b.base_reading = s.get_gas_meter_base(); }
    nvs_set_blob(n, "data", &b, sizeof(b));
    nvs_commit(n); nvs_close(n);
}
