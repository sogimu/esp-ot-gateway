#!/usr/bin/env python3
"""Add configurable MQTT status/stats intervals to the project."""
import re, sys

ROOT = "/home/as-lizin/develop/esp-ot-gateway-mqqt"

# ── 1. Add to IMqttConfigPersistence (save_mqtt_config gets intervals) ──
path = f"{ROOT}/main/application/ports/driven/imqtt_config_persistence.h"
with open(path) as f: c = f.read()
# save_mqtt_config: add status_interval_s, stats_interval_s params
old = "bool enabled, bool tls) = 0;"
new = "bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s) = 0;"
c = c.replace(old, new)
with open(path, "w") as f: f.write(c)
print("1. IMqttConfigPersistence updated")

# ── 2. Add to IMqttConfigurator (getters) ──
path = f"{ROOT}/main/application/ports/driving/imqtt_configurator.h"
with open(path) as f: c = f.read()
old = "virtual bool get_tls() const = 0;"
new = "virtual bool get_tls() const = 0;\n    virtual uint16_t get_status_interval_s() const = 0;\n    virtual uint16_t get_stats_interval_s() const = 0;"
c = c.replace(old, new)
with open(path, "w") as f: f.write(c)
print("2. IMqttConfigurator updated")

# ── 3. Update NvsConfigAdapter save/load ──
path = f"{ROOT}/main/infrastructure/driven/nvs_config_adapter.h"
with open(path) as f: c = f.read()
old = "bool enabled, bool tls) override;"
new = "bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s) override;"
c = c.replace(old, new)
# Add interval methods
old = "bool load_total_uptime(uint32_t& total_uptime_sec) override;"
new = "bool load_total_uptime(uint32_t& total_uptime_sec) override;\n\n    void save_mqtt_intervals(uint16_t status_s, uint16_t stats_s) override;\n    bool load_mqtt_intervals(uint16_t& status_s, uint16_t& stats_s) override;"
c = c.replace(old, new)
with open(path, "w") as f: f.write(c)
print("3. NvsConfigAdapter.h updated")

# ── 4. Update NvsConfigAdapter.cpp ──
path = f"{ROOT}/main/infrastructure/driven/nvs_config_adapter.cpp"
with open(path) as f: c = f.read()
# Update save_mqtt_config signature
old = "bool enabled, bool tls)"
new = "bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s)"
c = c.replace(old, new)
# Add NVS writes
old = 'nvs_set_u8(h, "mqtt_tls", tls ? 1 : 0);'
new = 'nvs_set_u8(h, "mqtt_tls", tls ? 1 : 0);\n    nvs_set_u16(h, "mqtt_sti", status_interval_s);\n    nvs_set_u16(h, "mqtt_ssi", stats_interval_s);'
c = c.replace(old, new)
# Add interval methods at end of file (before EOF)
old = "nvs_close(h);\n    return ok;\n}\n"
last = c.rfind("nvs_close(h);")
# Insert methods before the last function end
c += """
void NvsConfigAdapter::save_mqtt_intervals(uint16_t status_s, uint16_t stats_s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u16(h, "mqtt_sti", status_s);
    nvs_set_u16(h, "mqtt_ssi", stats_s);
    nvs_commit(h); nvs_close(h);
}

bool NvsConfigAdapter::load_mqtt_intervals(uint16_t& status_s, uint16_t& stats_s)
{
    nvs_handle_t h;
    if (nvs_open("config", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = false;
    uint16_t v;
    if (nvs_get_u16(h, "mqtt_sti", &v) == ESP_OK) { status_s = v; ok = true; }
    if (nvs_get_u16(h, "mqtt_ssi", &v) == ESP_OK) { stats_s = v; ok = true; }
    nvs_close(h);
    return ok;
}
"""
with open(path, "w") as f: f.write(c)
print("4. NvsConfigAdapter.cpp updated")

# ── 5. Update fake NVS config persistence ──
path = f"{ROOT}/test/fakes/fake_mqtt_config_persistence.h"
with open(path) as f: c = f.read()
old = "bool enabled, bool tls) override"
new = "bool enabled, bool tls, uint16_t, uint16_t) override"
c = c.replace(old, new)
# Add interval methods
old = "bool tls_ = false;"
new = "bool tls_ = false;\n    uint16_t status_interval_s_ = 30;\n    uint16_t stats_interval_s_ = 300;\n\n    void save_mqtt_intervals(uint16_t status_s, uint16_t stats_s) override {\n        status_interval_s_ = status_s; stats_interval_s_ = stats_s;\n    }\n    bool load_mqtt_intervals(uint16_t& status_s, uint16_t& stats_s) override {\n        status_s = status_interval_s_; stats_s = stats_interval_s_; return true;\n    }"
c = c.replace(old, new)
with open(path, "w") as f: f.write(c)
print("5. Fake config updated")

# ── 6. Update MqttInteractor ──
path = f"{ROOT}/main/infrastructure/driving/mqtt_interactor.h"
with open(path) as f: c = f.read()
# Add getters
old = "bool get_tls() const override         { return tls_; }"
new = "bool get_tls() const override         { return tls_; }\n    uint16_t get_status_interval_s() const override { return status_interval_s_; }\n    uint16_t get_stats_interval_s() const override  { return stats_interval_s_; }"
c = c.replace(old, new)
# Remove constexpr intervals
old = "static constexpr int PUBLISH_INTERVAL = 25;   // статус ~27с (25 × 1.1с)\n    static constexpr int STATS_INTERVAL  = 270;  // статистика ~5 мин (270 × 1.1с)"
new = "// Publish intervals now configurable via NVS/web UI\n    // Defaults: status ~27s (25 cycles), stats ~5min (270 cycles)"
c = c.replace(old, new)
# Add interval member variables
old = "bool     tls_         = false;"
new = "bool     tls_         = false;\n    uint16_t status_interval_s_ = 30;\n    uint16_t stats_interval_s_  = 300;"
c = c.replace(old, new)
with open(path, "w") as f: f.write(c)
print("6. MqttInteractor.h updated")

# ── 7. Update MqttInteractor.cpp ──
path = f"{ROOT}/main/infrastructure/driving/mqtt_interactor.cpp"
with open(path) as f: c = f.read()
# Add intervals to save_and_apply
old = "bool enabled, bool tls)"
new = "bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s)"
# This may not match — let me use a broader pattern
if old in c:
    c = c.replace(old, new)
else:
    print("WARNING: save_and_apply signature mismatch")

# Update save_and_apply to store intervals
old = "enabled_ = enabled;\n    tls_ = tls;"
new = "enabled_ = enabled;\n    tls_ = tls;\n    status_interval_s_ = status_interval_s;\n    stats_interval_s_ = stats_interval_s;"
c = c.replace(old, new)

# Update save_and_apply call to cfg_store
old = 'cfg_store_.save_mqtt_config(host_, port_, user_, pass_, prefix_, enabled_, tls_);'
new = 'cfg_store_.save_mqtt_config(host_, port_, user_, pass_, prefix_, enabled_, tls_, status_interval_s_, stats_interval_s_);'
c = c.replace(old, new)

# Add interval loading to init()
old = 'load_mqtt_config(host_, sizeof(host_), port_,'
new = 'load_mqtt_intervals(status_interval_s_, stats_interval_s_);\n    cfg_store_.load_mqtt_config(host_, sizeof(host_), port_,'
c = c.replace(old, new)

# Replace constexpr intervals with configurable ones in poll()
old = "if (poll_counter_ % PUBLISH_INTERVAL == 0) publish_status();"
new = "int pub_cycles = (int)(status_interval_s_ / 1.1f); if (pub_cycles < 1) pub_cycles = 1;\n        if (poll_counter_ % pub_cycles == 0) publish_status();"
c = c.replace(old, new)

old = "if (stats_tick_ % STATS_INTERVAL == 0) publish_stats();"
new = "int stats_cycles = (int)(stats_interval_s_ / 1.1f); if (stats_cycles < 1) stats_cycles = 1;\n        if (stats_tick_ % stats_cycles == 0) publish_stats();"
c = c.replace(old, new)

with open(path, "w") as f: f.write(c)
print("7. MqttInteractor.cpp updated")

# ── 8. Update HTTP handler to parse intervals ──
path = f"{ROOT}/main/infrastructure/driving/http_controller_adapter.cpp"
with open(path) as f: c = f.read()
# Parse interval fields
old = "int v_tls     = json_get_int(body, \"\\\"tls\\\"\");"
new = "int v_tls     = json_get_int(body, \"\\\"tls\\\"\");\n    int v_sti     = json_get_int(body, \"\\\"status_interval\\\"\");\n    int v_ssi     = json_get_int(body, \"\\\"stats_interval\\\"\");"
c = c.replace(old, new)

# Add default intervals to save call
old = 'self->mqtt_->save_and_apply(buf_host, (uint16_t)(v_port > 0 ? v_port : 1883), buf_user, buf_pass, buf_prefix, v_enabled > 0, v_tls > 0);'
new = 'self->mqtt_->save_and_apply(buf_host, (uint16_t)(v_port > 0 ? v_port : 1883), buf_user, buf_pass, buf_prefix, v_enabled > 0, v_tls > 0, (uint16_t)(v_sti > 0 ? v_sti : 30), (uint16_t)(v_ssi > 0 ? v_ssi : 300));'
c = c.replace(old, new)

# Update MQTT status response to include intervals
old = '"\\\"tls\\\":%s}"'
new = '"\\\"tls\\\":%s,\\\"status_interval\\\":%u,\\\"stats_interval\\\":%u}"'
c = c.replace(old, new)

old = 'self->mqtt_->get_tls()      ? "true" : "false");'
new = 'self->mqtt_->get_tls()      ? "true" : "false",\n        self->mqtt_->get_status_interval_s(),\n        self->mqtt_->get_stats_interval_s());'
c = c.replace(old, new)

with open(path, "w") as f: f.write(c)
print("8. HTTP handler updated")

# ── 9. Add interval fields to web page ──
path = f"{ROOT}/main/infrastructure/driving/web_page.h"
with open(path) as f: c = f.read()
# Add interval inputs before the TLS toggle
old = """<div class='ctrl-row'><label>TLS"""
new = """<div class='ctrl-row'><label>Интервал статуса <span class='tip'><span class='tip-icon'>&#9432;</span><span class='tiptext'>Период публикации status в секундах. Минимум 5, по умолчанию 30.</span></span></label><input type='number' id='mqtt_sti' value='30' min='5' max='3600' style='width:80px' oninput='setDirty()' onfocus=\"dirtyFields.mqtt=true\" onchange=\"dirtyFields.mqtt=false\"></div><div class='ctrl-row'><label>Интервал статистики <span class='tip'><span class='tip-icon'>&#9432;</span><span class='tiptext'>Период публикации stats в секундах. Минимум 30, по умолчанию 300.</span></span></label><input type='number' id='mqtt_ssi' value='300' min='30' max='86400' style='width:80px' oninput='setDirty()' onfocus=\"dirtyFields.mqtt=true\" onchange=\"dirtyFields.mqtt=false\"></div><div class='ctrl-row'><label>TLS"""
c = c.replace(old, new)

# Update mqttPoll to read interval values
old = "document.getElementById('mqtt_tls').checked=d.tls;"
new = "document.getElementById('mqtt_tls').checked=d.tls;\nif(d.status_interval)document.getElementById('mqtt_sti').value=d.status_interval;\nif(d.stats_interval)document.getElementById('mqtt_ssi').value=d.stats_interval;"
c = c.replace(old, new)

# Update mqttSave to include intervals
old = "tls:document.getElementById('mqtt_tls').checked?1:0});"
new = "tls:document.getElementById('mqtt_tls').checked?1:0,status_interval:parseInt(document.getElementById('mqtt_sti').value)||30,stats_interval:parseInt(document.getElementById('mqtt_ssi').value)||300});"
c = c.replace(old, new)

with open(path, "w") as f: f.write(c)
print("9. Web page updated")

print("\n=== ALL DONE ===")
