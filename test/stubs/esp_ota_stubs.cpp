#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_err.h"

// Две OTA-партиции (A/B) для тестов итератора.
esp_partition_t g_ota_stub_partitions[2] = {{"ota_0"}, {"ota_1"}};
const esp_partition_t* g_ota_stub_running      = &g_ota_stub_partitions[1];
bool                   g_ota_stub_pending_verify = false;
bool                   g_ota_stub_mark_valid_ok  = true;
bool                   g_ota_stub_mark_invalid_ok = true;
int64_t                g_ota_stub_timer_us       = 0;

const esp_partition_t* esp_ota_get_running_partition(void) {
    return g_ota_stub_running;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* st) {
    *st = g_ota_stub_pending_verify ? ESP_OTA_IMG_PENDING_VERIFY : ESP_OTA_IMG_UNDEFINED;
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {
    g_ota_stub_pending_verify = false;
    return g_ota_stub_mark_valid_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) {
    if (!g_ota_stub_pending_verify)
        return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    g_ota_stub_pending_verify = false;
    return g_ota_stub_mark_invalid_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
    return ESP_OK;
}

// ── Partition iterator ─────────────────────────────────────
struct esp_partition_opaque {
    int idx;
};

const esp_partition_t* esp_partition_get(esp_partition_iterator_t it) {
    if (!it || it->idx < 0 || it->idx >= 2) return nullptr;
    return &g_ota_stub_partitions[it->idx];
}

esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it) {
    if (!it) return nullptr;
    int next = it->idx + 1;
    delete it;
    if (next >= 2) return nullptr;
    return new esp_partition_opaque{next};
}

esp_partition_iterator_t esp_partition_find(esp_partition_type_t,
                                             esp_partition_subtype_t,
                                             const char*) {
    return new esp_partition_opaque{0};
}

void esp_partition_iterator_release(esp_partition_iterator_t it) {
    delete it;
}

int64_t esp_timer_get_time(void) {
    return g_ota_stub_timer_us;
}
