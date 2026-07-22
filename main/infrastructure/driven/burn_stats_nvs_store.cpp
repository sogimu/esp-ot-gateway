#include "infrastructure/driven/burn_stats_nvs_store.h"
#include "nvs.h"

// ── "stats" namespace — скалярная статистика горелки ──────────────────────

bool BurnStatsNvsStore::load_burn_stats(uint32_t& burner_sec, uint32_t& total_pause_sec, uint32_t& cycle_cnt,
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

void BurnStatsNvsStore::save_burn_stats(uint32_t burner_sec, uint32_t total_pause_sec, uint32_t cycle_cnt,
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
