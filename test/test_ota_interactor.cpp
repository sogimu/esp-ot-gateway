/// Хостовые тесты OtaInteractor. Зависят только от портов + fakes, без аппаратуры.
/// Проверяют: конечный автомат, кэширование версий, блокировку повторного begin_update,
/// загрузку через run_download, синхронизацию прогресса/ошибки, ручной откат.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <cstring>

#include "infrastructure/driving/ota_interactor.h"
#include "fakes/fake_ota_collaborators.h"

/// TestOtaInteractor: швы записывают признаки (без FreeRTOS/перезагрузки).
/// test_run_download() — публичная обёртка над защищённым run_download().
struct TestOtaInteractor : public OtaInteractor {
    using OtaInteractor::OtaInteractor;

    bool launch_download_task() override { launch_called = true; return launch_ok; }
    void reboot_into_new_slot() override { reboot_called = true; }

    bool launch_called   = false;
    bool launch_ok       = true;
    bool reboot_called   = false;

    /// Публичная обёртка для вызова run_download из тестов.
    void test_run_download() { run_download(); }
};

TEST_CASE("OtaInteractor: конструктор — IDLE, версия, rollback_pending live", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    val.pending = false;
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    OtaStatus s = ota.status();
    REQUIRE(s.state == OtaStatus::IDLE);
    REQUIRE(s.rollback_pending == false);
    REQUIRE(std::strlen(s.current_version) > 0);  // FIRMWARE_VERSION инъецирован
}

TEST_CASE("OtaInteractor: конструктор — VERIFY_PENDING когда образ на проверке", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    val.pending = true;
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    OtaStatus s = ota.status();
    REQUIRE(s.state == OtaStatus::VERIFY_PENDING);
    REQUIRE(s.rollback_pending == true);
}

TEST_CASE("OtaInteractor: status() отдаёт живой rollback_pending", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    REQUIRE(ota.status().rollback_pending == false);

    val.pending = true;
    REQUIRE(ota.status().rollback_pending == true);
}

TEST_CASE("OtaInteractor: begin_update nullptr/пустой — отказ", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    REQUIRE_FALSE(ota.begin_update(nullptr));
    REQUIRE_FALSE(ota.begin_update(""));
    REQUIRE(ota.status().state == OtaStatus::IDLE);
}

TEST_CASE("OtaInteractor: begin_update IDLE → FETCHING + target_tag", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    REQUIRE(ota.begin_update("v1.0.0"));
    REQUIRE(ota.launch_called == true);

    OtaStatus s = ota.status();
    REQUIRE(s.state == OtaStatus::FETCHING);
    REQUIRE(std::strcmp(s.target_tag, "v1.0.0") == 0);
}

TEST_CASE("OtaInteractor: begin_update отклоняется когда занят (FETCHING)", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    ota.begin_update("v1");
    REQUIRE(ota.status().state == OtaStatus::FETCHING);
    REQUIRE_FALSE(ota.begin_update("v2"));
}

TEST_CASE("OtaInteractor: begin_update отклоняется в VERIFY_PENDING", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    val.pending = true;
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    REQUIRE(ota.status().state == OtaStatus::VERIFY_PENDING);
    REQUIRE_FALSE(ota.begin_update("v1"));
}

TEST_CASE("OtaInteractor: begin_update после DONE разрешён", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.begin_update("v1");
    ota.test_run_download();  // ok → DONE
    REQUIRE(ota.status().state == OtaStatus::DONE);

    REQUIRE(ota.begin_update("v2"));
    REQUIRE(ota.status().state == OtaStatus::FETCHING);
}

TEST_CASE("OtaInteractor: begin_update после ERROR разрешён", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    dl.should_succeed = false;
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.begin_update("v1");
    ota.test_run_download();
    REQUIRE(ota.status().state == OtaStatus::ERROR);

    REQUIRE(ota.begin_update("v2"));
    REQUIRE(ota.status().state == OtaStatus::FETCHING);
}

TEST_CASE("OtaInteractor: launch fails → ERROR", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.launch_ok = false;

    REQUIRE_FALSE(ota.begin_update("v1"));
    REQUIRE(ota.status().state == OtaStatus::ERROR);
    REQUIRE(std::strlen(ota.status().last_error) > 0);
}

TEST_CASE("OtaInteractor: run_download успех → DONE + progress 100 + reboot", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;
    bool                  flushed = false;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.set_flush_stats_callback([](void* ctx) { *static_cast<bool*>(ctx) = true; }, &flushed);

    ota.begin_update("v1");
    ota.test_run_download();

    OtaStatus s = ota.status();
    REQUIRE(s.state == OtaStatus::DONE);
    REQUIRE(s.progress_pct == 100);
    REQUIRE(ota.reboot_called == true);
    REQUIRE(flushed == true);
}

TEST_CASE("OtaInteractor: run_download провал → ERROR + last_error, без ребута", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    dl.should_succeed = false;
    std::strncpy(dl.error, "тестовая ошибка загрузки", sizeof(dl.error));

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.begin_update("v1");
    ota.test_run_download();

    OtaStatus s = ota.status();
    REQUIRE(s.state == OtaStatus::ERROR);
    // last_error должен содержать либо ошибку downloader, либо fallback
    REQUIRE(std::strlen(s.last_error) > 0);
    REQUIRE(ota.reboot_called == false);
}

TEST_CASE("OtaInteractor: poll — прогресс из downloader мапируется", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.begin_update("v1");
    // Имитируем прогресс downloader (без реальной загрузки).
    dl.pct = 42;
    ota.poll();

    OtaStatus s = ota.status();  // state ещё FETCHING (загрузка не завершена)
    REQUIRE(s.progress_pct == 42);
}

TEST_CASE("OtaInteractor: poll — last_error из downloader мапируется", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    std::strncpy(dl.error, "сетевая ошибка", sizeof(dl.error));
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.begin_update("v1");
    ota.poll();

    OtaStatus s = ota.status();
    REQUIRE(std::strstr(s.last_error, "сетевая ошибка") != nullptr);
}

TEST_CASE("OtaInteractor: rollback_now → validity.mark_invalid_and_reboot", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });
    ota.rollback_now();

    REQUIRE(val.reboot_called == true);
}

TEST_CASE("OtaInteractor: fetch_versions — кэш на 5 мин, перезапрос после TTL", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    const char* body = "{\"versions\":[{\"tag\":\"v0.5.0\"}]}";
    ver.next_response = strdup(body);

    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    // Первый вызов: загрузка (fetch_count == 1).
    char* r1 = ota.fetch_version_list();
    REQUIRE(r1 != nullptr);
    REQUIRE(std::strstr(r1, "v0.5.0") != nullptr);
    free(r1);
    REQUIRE(ver.fetch_count == 1);

    // Второй вызов внутри TTL: кэш (fetch_count не меняется).
    char* r2 = ota.fetch_version_list();
    REQUIRE(r2 != nullptr);
    free(r2);
    REQUIRE(ver.fetch_count == 1);

    // Продвигаем время за TTL (5 мин = 300 000 мс).
    now += 300'001;

    // Третий вызов после TTL: перезагрузка (fetch_count == 2).
    char* r3 = ota.fetch_version_list();
    REQUIRE(r3 != nullptr);
    free(r3);
    REQUIRE(ver.fetch_count == 2);
}

TEST_CASE("OtaInteractor: fetch_versions — nullptr при ошибке транспорта", "[ota]") {
    FakeOtaValidity      val;
    FakeOtaDownloader     dl;
    FakeOtaVersionIndex   ver;
    int64_t               now = 0;

    // next_response = nullptr (по умолчанию) → fetch_versions возвращает nullptr.
    TestOtaInteractor ota(val, dl, ver, [&]() { return now; });

    REQUIRE(ota.fetch_version_list() == nullptr);
    REQUIRE(ver.fetch_count == 1);
}
