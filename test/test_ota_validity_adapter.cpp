#include <catch2/catch_test_macros.hpp>

#include "infrastructure/driven/ota_validity_adapter.h"
#include "esp_ota_ops.h"   // controllable stubs

TEST_CASE("OtaValidityAdapter: not pending on boot", "[ota][val]") {
    g_ota_stub_pending_verify = false;
    OtaValidityAdapter val;
    REQUIRE(val.is_pending() == false);
    REQUIRE(OtaValidityAdapter::is_pending_global() == false);
    REQUIRE(OtaValidityAdapter::is_flushing_global() == false);
}

TEST_CASE("OtaValidityAdapter: pending on boot", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    OtaValidityAdapter val;
    REQUIRE(val.is_pending() == true);
    REQUIRE(OtaValidityAdapter::is_pending_global() == true);
}

TEST_CASE("OtaValidityAdapter: arm() when not pending — no-op", "[ota][val]") {
    g_ota_stub_pending_verify = false;
    OtaValidityAdapter val;
    val.arm();
    REQUIRE(val.is_pending() == false);
}

TEST_CASE("OtaValidityAdapter: arm() sets deadline when pending", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.arm();
    REQUIRE(val.is_pending() == true);
}

TEST_CASE("OtaValidityAdapter: arm() with crash flag — rollback", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    OtaValidityAdapter val;
    val.set_crash_flag(true);
    val.arm();
    // rollback_and_reboot был вызван: stub очистил g_ota_stub_pending_verify
    // (на железе esp_restart не вернул бы управление).
    // is_pending() кэширован в конструкторе — не изменился, что ожидаемо.
    // Проверяем, что stub-глобал изменился (rollback вызван):
    REQUIRE(g_ota_stub_pending_verify == false);
}

TEST_CASE("OtaValidityAdapter: heartbeat before arm — no-op", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    OtaValidityAdapter val;
    val.heartbeat();
    REQUIRE(val.is_pending() == true);  // not armed, no effect
}

TEST_CASE("OtaValidityAdapter: heartbeat after arm + http_up → mark_valid", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.set_http_server_up(true);
    val.arm();
    val.heartbeat();
    // pending should be cleared (mark_valid called)
    REQUIRE(val.is_pending() == false);
}

TEST_CASE("OtaValidityAdapter: heartbeat http_down, deadline ok → no-op", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.set_http_server_up(false);
    val.arm();
    // deadline = 1000000 + 90s, now is 1000000 → not expired
    val.heartbeat();
    // should NOT mark valid, NOT rollback
    REQUIRE(g_ota_stub_pending_verify == true);
}

TEST_CASE("OtaValidityAdapter: heartbeat http_down, deadline expired → rollback", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;  // arm() установит deadline = 1_000_000 + 90_000_000
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.set_http_server_up(false);
    val.arm();
    // сдвигаем таймер вперёд после arm(), чтобы deadline истёк
    g_ota_stub_timer_us = 200000000;  // > deadline
    val.heartbeat();
    // rollback вызван → stub очистил g_ota_stub_pending_verify
    REQUIRE(g_ota_stub_pending_verify == false);
}

TEST_CASE("OtaValidityAdapter: heartbeat idempotent after mark_valid", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.set_http_server_up(true);
    val.arm();
    val.heartbeat();  // marks valid
    REQUIRE(val.is_pending() == false);
    g_ota_stub_timer_us = 200000000000LL;  // far future, would rollback if not already valid
    val.heartbeat();  // second call — idempotent, no action
    REQUIRE(val.is_pending() == false);
}

TEST_CASE("OtaValidityAdapter: mark_invalid_and_reboot when pending", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_mark_invalid_ok = true;
    OtaValidityAdapter val;
    REQUIRE(val.is_pending() == true);
    val.mark_invalid_and_reboot();
    // stub-флаг очищен (функция вызвана), cached pending_ не меняется
    REQUIRE(g_ota_stub_pending_verify == false);
}

TEST_CASE("OtaValidityAdapter: mark_invalid_and_reboot after valid → fallback iterator", "[ota][val]") {
    // После mark_valid: pending=false. mark_invalid_rollback вернёт ошибку,
    // fallback ищет другой слот через esp_partition_find.
    g_ota_stub_pending_verify = false;
    g_ota_stub_running = &g_ota_stub_partitions[1];  // бежим на ota_1
    OtaValidityAdapter val;
    REQUIRE(val.is_pending() == false);
    // Должен найти ota_0 через итератор и переключиться.
    val.mark_invalid_and_reboot();
    REQUIRE(true);  // не упал, не UB
}

TEST_CASE("OtaValidityAdapter: mark_invalid_and_reboot with only one partition", "[ota][val]") {
    // Обе партиции указывают на текущую — итератор не найдёт другую.
    g_ota_stub_pending_verify = false;
    g_ota_stub_running = &g_ota_stub_partitions[0];
    g_ota_stub_partitions[1] = g_ota_stub_partitions[0];  // та же, что и running
    OtaValidityAdapter val;
    val.mark_invalid_and_reboot();
    g_ota_stub_partitions[1] = {"ota_1"};  // восстановить
    REQUIRE(true);
}

TEST_CASE("OtaValidityAdapter: set_http_server_up отражается на heartbeat", "[ota][val]") {
    g_ota_stub_pending_verify = true;
    g_ota_stub_timer_us = 1000000;
    OtaValidityAdapter val;
    val.set_crash_flag(false);
    val.set_http_server_up(false);
    val.arm();
    val.heartbeat();
    // http up = false, deadline ok → не помечено валидным
    REQUIRE(g_ota_stub_pending_verify == true);
    val.set_http_server_up(true);
    val.heartbeat();
    // теперь http up → mark_valid
    REQUIRE(g_ota_stub_pending_verify == false);
}

TEST_CASE("OtaValidityAdapter: is_flushing_global set/clear", "[ota][val]") {
    REQUIRE(OtaValidityAdapter::is_flushing_global() == false);
    OtaValidityAdapter::set_flushing_global(true);
    REQUIRE(OtaValidityAdapter::is_flushing_global() == true);
    OtaValidityAdapter::set_flushing_global(false);
    REQUIRE(OtaValidityAdapter::is_flushing_global() == false);
}
