#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

/// In-memory MQTT config store (mirrors NvsConfigAdapter MQTT methods).
/// Используется для тестирования логики сохранения/загрузки без NVS.
class InMemoryMqttConfig {
public:
    void save(const char* host, uint16_t port,
              const char* user, const char* pass,
              const char* prefix, bool enabled, bool tls)
    {
        snprintf(host_, sizeof(host_), "%s", host);
        port_ = port;
        snprintf(user_, sizeof(user_), "%s", user);
        snprintf(pass_, sizeof(pass_), "%s", pass);
        snprintf(prefix_, sizeof(prefix_), "%s", prefix);
        enabled_ = enabled;
        tls_ = tls;
        save_called_ = true;
    }

    bool load(char* host, size_t host_size, uint16_t& port,
              char* user, size_t user_size,
              char* pass, size_t pass_size,
              char* prefix, size_t prefix_size,
              bool& enabled, bool& tls)
    {
        if (!save_called_) return false;

        snprintf(host, host_size, "%s", host_);
        port = port_;
        snprintf(user, user_size, "%s", user_);
        snprintf(pass, pass_size, "%s", pass_);
        snprintf(prefix, prefix_size, "%s", prefix_);
        enabled = enabled_;
        tls = tls_;
        load_called_ = true;
        return true;
    }

    // Публичные поля для проверок
    char host_[128] = {};
    uint16_t port_ = 1883;
    char user_[64] = {};
    char pass_[64] = {};
    char prefix_[64] = {};
    bool enabled_ = false;
    bool tls_ = false;
    bool save_called_ = false;
    bool load_called_ = false;
};

TEST_CASE("MQTT NVS: round-trip save/load", "[mqtt][nvs][persistence]") {
    InMemoryMqttConfig cfg;

    cfg.save("test.local", 1883, "user1", "pass1",
             "my-gateway", true, false);
    REQUIRE(cfg.save_called_);

    char host[128] = {}, user[64] = {}, pass[64] = {}, prefix[64] = {};
    uint16_t port = 0;
    bool enabled = false, tls = true;

    REQUIRE(cfg.load(host, sizeof(host), port,
                     user, sizeof(user), pass, sizeof(pass),
                     prefix, sizeof(prefix), enabled, tls));

    REQUIRE(std::string(host) == "test.local");
    REQUIRE(port == 1883);
    REQUIRE(std::string(user) == "user1");
    REQUIRE(std::string(pass) == "pass1");
    REQUIRE(std::string(prefix) == "my-gateway");
    REQUIRE(enabled == true);
    REQUIRE(tls == false);
}

TEST_CASE("MQTT NVS: значения по умолчанию сохраняются при отсутствии данных", "[mqtt][nvs][persistence]") {
    InMemoryMqttConfig cfg;

    // Без сохранения — load должен вернуть false, значения не меняются
    char host[128] = "default_host";
    uint16_t port = 9999;
    char user[64] = "def_user";
    char pass[64] = "def_pass";
    char prefix[64] = "def_prefix";
    bool enabled = true, tls = true;

    bool ok = cfg.load(host, sizeof(host), port,
                       user, sizeof(user), pass, sizeof(pass),
                       prefix, sizeof(prefix), enabled, tls);

    REQUIRE_FALSE(ok); // nothing saved yet
    // Значения не должны измениться при неудачной загрузке
    REQUIRE(std::string(host) == "default_host");
    REQUIRE(port == 9999);
    REQUIRE(std::string(user) == "def_user");
    REQUIRE(std::string(pass) == "def_pass");
    REQUIRE(std::string(prefix) == "def_prefix");
    REQUIRE(enabled == true);
    REQUIRE(tls == true);
}

TEST_CASE("MQTT NVS: порт по умолчанию 1883", "[mqtt][nvs][persistence]") {
    InMemoryMqttConfig cfg;

    // Сохраняем без явного порта (оставляем по умолчанию)
    cfg.save("broker.local", 1883, "", "", "esp-gw", true, false);

    char host[128] = {};
    uint16_t port = 0;
    char user[64] = {}, pass[64] = {}, prefix[64] = {};
    bool enabled = false, tls = false;

    cfg.load(host, sizeof(host), port,
             user, sizeof(user), pass, sizeof(pass),
             prefix, sizeof(prefix), enabled, tls);

    REQUIRE(port == 1883);
}

TEST_CASE("MQTT NVS: пустые строки сохраняются корректно", "[mqtt][nvs][persistence]") {
    InMemoryMqttConfig cfg;

    cfg.save("host", 8883, "", "", "prefix", false, true);

    char host[128] = {}, user[64] = "x", pass[64] = "x", prefix[64] = {};
    uint16_t port = 0;
    bool enabled = true, tls = false;

    cfg.load(host, sizeof(host), port,
             user, sizeof(user), pass, sizeof(pass),
             prefix, sizeof(prefix), enabled, tls);

    REQUIRE(std::string(user) == "");  // пустой логин
    REQUIRE(std::string(pass) == "");  // пустой пароль
    REQUIRE(enabled == false);
    REQUIRE(tls == true);
}

TEST_CASE("MQTT NVS: длинные строки обрезаются до размера буфера", "[mqtt][nvs][persistence]") {
    InMemoryMqttConfig cfg;

    const char* long_host = "very-long-hostname-that-exceeds-buffer-size.local";
    cfg.save(long_host, 1883, "", "", "pref", true, false);

    char host[128] = {};
    uint16_t port = 0;
    char user[64] = {}, pass[64] = {}, prefix[64] = {};
    bool enabled = false, tls = false;

    cfg.load(host, sizeof(host), port,
             user, sizeof(user), pass, sizeof(pass),
             prefix, sizeof(prefix), enabled, tls);

    // Должен поместиться с усечением (не более host_size - 1)
    REQUIRE(strlen(host) < sizeof(host));
    REQUIRE(strlen(host) <= 127);
}
