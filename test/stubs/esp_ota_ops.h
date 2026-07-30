#pragma once

#include "esp_err.h"
#include <cstdint>

// ── esp_partition.h types ──────────────────────────────────
struct esp_partition_t {
    const char* label;
};

typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;

#define ESP_PARTITION_TYPE_APP    0
#define ESP_PARTITION_SUBTYPE_ANY 0xFF

struct esp_partition_opaque;
using esp_partition_iterator_t = const esp_partition_opaque*;

const esp_partition_t* esp_partition_get(esp_partition_iterator_t it);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it);
esp_partition_iterator_t esp_partition_find(esp_partition_type_t type,
                                             esp_partition_subtype_t subtype,
                                             const char* label);
void esp_partition_iterator_release(esp_partition_iterator_t it);

// ── esp_ota_ops.h types ────────────────────────────────────
typedef enum {
    ESP_OTA_IMG_UNDEFINED = 0,
    ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_PENDING_VERIFY,
} esp_ota_img_states_t;

// ── Controllable stub globals ──────────────────────────────
extern esp_partition_t       g_ota_stub_partitions[2];
extern const esp_partition_t* g_ota_stub_running;
extern bool                   g_ota_stub_pending_verify;
extern bool                   g_ota_stub_mark_valid_ok;
extern bool                   g_ota_stub_mark_invalid_ok;
extern int64_t                g_ota_stub_timer_us;

// ── Declarations (implemented in esp_ota_stubs.cpp) ────────
const esp_partition_t* esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);
