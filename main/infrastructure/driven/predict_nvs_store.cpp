#include "infrastructure/driven/predict_nvs_store.h"
#include "infrastructure/driven/nvs_config_store.h"  // NvsPredictBlob
#include "infrastructure/driven/ota_validity_adapter.h"  // is_pending_global()

#include "nvs.h"
#include <cstring>

// ── "predict" namespace ────────────────────────────────────────────────────

bool PredictNvsStore::load_predict(float r[3], int& idx, int& cnt)
{
    nvs_handle_t n;
    if (nvs_open("predict", NVS_READONLY, &n) != ESP_OK) return false;
    NvsPredictBlob b; memset(&b, 0, sizeof(b));
    size_t sz = 0;
    if (nvs_get_blob(n, "dhw_hist", nullptr, &sz) == ESP_OK && sz == sizeof(b)) {
        if (nvs_get_blob(n, "dhw_hist", &b, &sz) == ESP_OK) {
            memcpy(r, b.rates, 3*sizeof(float)); idx = (int)b.idx; cnt = (int)b.count;
        }
    }
    nvs_close(n); return true;
}

void PredictNvsStore::save_predict(const float r[3], int idx, int cnt)
{
    // D9: заморозка во время PENDING_VERIFY
    if (OtaValidityAdapter::is_pending_global()) return;

    nvs_handle_t n;
    if (nvs_open("predict", NVS_READWRITE, &n) != ESP_OK) return;
    NvsPredictBlob b;
    memcpy(b.rates, r, 3*sizeof(float)); b.idx = (int32_t)idx; b.count = (int32_t)cnt;
    nvs_set_blob(n, "dhw_hist", &b, sizeof(b));
    nvs_commit(n); nvs_close(n);
}
