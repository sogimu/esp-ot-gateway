/// Хостовые тесты OtaInteractor. SpawnFn/RebootFn — лямбды с захваченными флагами.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <cstring>

#include "infrastructure/driving/ota_interactor.h"
#include "fakes/fake_ota_collaborators.h"

TEST_CASE("OtaInteractor: конструктор — IDLE, версия, rollback_pending live", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; bool launch=false, reboot=false;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [&](OtaInteractor*){ launch=true; return true; }, [&]{ reboot=true; });
    auto s = ota.status();
    REQUIRE(s.state == OtaStatus::IDLE);
    REQUIRE(s.rollback_pending == false);
    REQUIRE(std::strlen(s.current_version) > 0);
}

TEST_CASE("OtaInteractor: конструктор — VERIFY_PENDING", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; val.pending = true;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    REQUIRE(ota.status().state == OtaStatus::VERIFY_PENDING);
}

TEST_CASE("OtaInteractor: status() отдаёт живой rollback_pending", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    REQUIRE_FALSE(ota.status().rollback_pending);
    val.pending = true;
    REQUIRE(ota.status().rollback_pending);
}

TEST_CASE("OtaInteractor: begin_update nullptr/пустой — отказ", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    REQUIRE_FALSE(ota.begin_update(nullptr));
    REQUIRE_FALSE(ota.begin_update(""));
}

TEST_CASE("OtaInteractor: begin_update IDLE → FETCHING + target_tag", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; bool launched = false;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [&](OtaInteractor*){ launched=true; return true; }, [](){});
    REQUIRE(ota.begin_update("v1.0.0"));
    REQUIRE(launched);
    REQUIRE(ota.status().state == OtaStatus::FETCHING);
    REQUIRE(std::strcmp(ota.status().target_tag, "v1.0.0") == 0);
}

TEST_CASE("OtaInteractor: begin_update отклоняется когда занят", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    ota.begin_update("v1");
    REQUIRE_FALSE(ota.begin_update("v2"));
}

TEST_CASE("OtaInteractor: begin_update отклоняется в VERIFY_PENDING", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; val.pending = true;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    REQUIRE_FALSE(ota.begin_update("v1"));
}

TEST_CASE("OtaInteractor: begin_update после DONE разрешён", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [&](OtaInteractor* s){ s->run_download(); return true; }, [](){});
    ota.begin_update("v1");
    REQUIRE(ota.status().state == OtaStatus::DONE);
    REQUIRE(ota.begin_update("v2"));
}

TEST_CASE("OtaInteractor: begin_update после ERROR разрешён", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; dl.should_succeed = false;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [&](OtaInteractor* s){ s->run_download(); return true; }, [](){});
    ota.begin_update("v1");
    REQUIRE(ota.status().state == OtaStatus::ERROR);
    REQUIRE(ota.begin_update("v2"));
}

TEST_CASE("OtaInteractor: spawn fails → ERROR", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return false; }, [](){});
    REQUIRE_FALSE(ota.begin_update("v1"));
    REQUIRE(ota.status().state == OtaStatus::ERROR);
}

TEST_CASE("OtaInteractor: run_download успех → DONE + progress 100 + reboot", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; bool rebooted = false;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [&]{ rebooted = true; });
    ota.begin_update("v1");
    ota.run_download();
    REQUIRE(ota.status().state == OtaStatus::DONE);
    REQUIRE(ota.status().progress_pct == 100);
    REQUIRE(rebooted);
}

TEST_CASE("OtaInteractor: run_download провал → ERROR + last_error, без ребута", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; bool rebooted = false;
    dl.should_succeed = false;
    std::strncpy(dl.error, "тестовая ошибка", sizeof(dl.error));
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [&]{ rebooted = true; });
    ota.begin_update("v1");
    ota.run_download();
    REQUIRE(ota.status().state == OtaStatus::ERROR);
    REQUIRE(std::strlen(ota.status().last_error) > 0);
    REQUIRE_FALSE(rebooted);
}

TEST_CASE("OtaInteractor: poll — прогресс мапируется", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0; dl.pct = 42;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    ota.begin_update("v1");
    ota.execute();
    REQUIRE(ota.status().progress_pct == 42);
}

TEST_CASE("OtaInteractor: poll — last_error мапируется", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    std::strncpy(dl.error, "сетевая ошибка", sizeof(dl.error));
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    ota.begin_update("v1");
    ota.execute();
    REQUIRE(std::strstr(ota.status().last_error, "сетевая ошибка") != nullptr);
}

TEST_CASE("OtaInteractor: rollback_now → validity.mark_invalid_and_reboot", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    ota.rollback_now();
    REQUIRE(val.reboot_called == true);
}

TEST_CASE("OtaInteractor: fetch_versions — кэш 5 мин, перезапрос после TTL", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    const char* body = R"({"versions":[{"tag":"v0.5.0"}]})";
    ver.next_response = strdup(body);
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    auto* r1 = ota.fetch_version_list();
    REQUIRE(r1); REQUIRE(std::strstr(r1, "v0.5.0")); free(r1);
    REQUIRE(ver.fetch_count == 1);
    auto* r2 = ota.fetch_version_list(); REQUIRE(r2); free(r2);
    REQUIRE(ver.fetch_count == 1);  // кэш
    now += 300'001;
    auto* r3 = ota.fetch_version_list(); REQUIRE(r3); free(r3);
    REQUIRE(ver.fetch_count == 2);  // перезагрузка после TTL
}

TEST_CASE("OtaInteractor: fetch_versions — nullptr при ошибке", "[ota]") {
    FakeOtaValidity val; FakeOtaDownloader dl; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, dl, ver, [&]{ return now; },
        [](OtaInteractor*){ return true; }, [](){});
    REQUIRE(ota.fetch_version_list() == nullptr);
}
