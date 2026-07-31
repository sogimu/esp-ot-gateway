#include "infrastructure/driven/gas_correction_nvs_store.h"

#include "infrastructure/driven/nvs_config_store.h"       // NvsMeterBlob, NvsCorrLogEntry, CORRECTION_LOG_SIZE
#include "infrastructure/driven/ota_validity_adapter.h"    // is_pending_global() — D9 freeze
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iboiler_config_store.h"

#include "nvs.h"
#include <cstring>

// ── "meter" namespace ──────────────────────────────────────────────────────

bool GasCorrectionNvsStore::load_meter(IHeatingStateStore& s, void* blob)
{
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READONLY, &n) != ESP_OK) return false;
    NvsMeterBlob b;
    memset(&b, 0, sizeof(b));
    bool ok = false;

    // D10: двухшаговый запрос размера
    size_t sz = 0;
    if (nvs_get_blob(n, "data", nullptr, &sz) == ESP_OK) {
        if (sz == sizeof(b)) {
            nvs_get_blob(n, "data", &b, &sz);
            ok = true;
        } else {
            // Legacy format: 32-entry correction log
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
                    for (int i = 0; i < keep; i++) {
                        int src = (old_head - keep + i + 32) % 32;
                        b.corrections[i] = old.corrections[src];
                    }
                    b.corrections_head = keep % 10;
                }
                ok = true;
            }
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

void GasCorrectionNvsStore::save_meter(const IHeatingStateStore& s, const void* blob)
{
    // D9: заморозка во время PENDING_VERIFY
    if (OtaValidityAdapter::is_pending_global()) return;

    nvs_handle_t n;
    if (nvs_open("meter", NVS_READWRITE, &n) != ESP_OK) return;
    NvsMeterBlob b;
    if (blob) {
        memcpy(&b, blob, sizeof(NvsMeterBlob));
        b.base_reading = s.get_gas_meter_base();  // state is authoritative
    } else {
        memset(&b, 0, sizeof(b));
        b.base_reading = s.get_gas_meter_base();
    }
    nvs_set_blob(n, "data", &b, sizeof(b));
    nvs_commit(n); nvs_close(n);
}

// ── "stats" namespace — integral_m3 ────────────────────────────────────────

void GasCorrectionNvsStore::save_integral(float value)
{
    if (OtaValidityAdapter::is_pending_global()) return;

    nvs_handle_t n;
    if (nvs_open("stats", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_blob(n, "integ_m3", &value, sizeof(value));
    nvs_commit(n);
    nvs_close(n);
}

// ── Boiler config — делегация ──────────────────────────────────────────────

void GasCorrectionNvsStore::save_boiler_config(const IHeatingStateStore& state)
{
    if (boiler_) boiler_->save_boiler_config(state);
}

// ── daily gas (meter namespace) ───────────────────────────────────────────

bool GasCorrectionNvsStore::load_daily_gas(void* blob)
{
    if (!blob) return false;
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READONLY, &n) != ESP_OK) return false;
    size_t sz = 0;
    bool ok = false;
    if (nvs_get_blob(n, "daily", nullptr, &sz) == ESP_OK && sz == sizeof(GasDailyBlob)) {
        nvs_get_blob(n, "daily", blob, &sz);
        ok = true;
    }
    nvs_close(n);
    return ok;
}

void GasCorrectionNvsStore::save_daily_gas(const void* blob)
{
    if (!blob || OtaValidityAdapter::is_pending_global()) return;
    nvs_handle_t n;
    if (nvs_open("meter", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_blob(n, "daily", blob, sizeof(GasDailyBlob));
    nvs_commit(n); nvs_close(n);
}
