#pragma once

#include "application/ports/driven/imqtt_config_store.h"
#include <cstddef>
#include <cstdint>

/// NVS-backed MQTT config store.
/// Uses "config" namespace. Keys: mqtt_host, mqtt_port, mqtt_user, mqtt_pass,
/// mqtt_pref, mqtt_en, mqtt_tls, mqtt_sti, mqtt_ssi.
class MqttNvsStore : public IMqttConfigStore {
public:
    void init();   // nvs_flash_init

    void save_mqtt_config(const char* host, uint16_t port,
                          const char* user, const char* pass,
                          const char* prefix, bool enabled, bool tls, uint16_t status_interval_s, uint16_t stats_interval_s) override;
    bool load_mqtt_config(char* host, size_t host_size,
                          uint16_t& port,
                          char* user, size_t user_size,
                          char* pass, size_t pass_size,
                          char* prefix, size_t prefix_size,
                          bool& enabled, bool& tls) override;

    void save_mqtt_intervals(uint16_t status_s, uint16_t stats_s) override;
    bool load_mqtt_intervals(uint16_t& status_s, uint16_t& stats_s) override;
};
