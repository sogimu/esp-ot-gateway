/// Хостовые тесты OtaInteractor (версии, кэш, rollback, SHA256).

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <cstring>

#include "infrastructure/driving/ota_interactor.h"
#include "fakes/fake_ota_collaborators.h"

TEST_CASE("OtaInteractor: конструктор — IDLE, версия, rollback_pending live", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, ver, [&]{ return now; });
    auto s = ota.status();
    REQUIRE(s.state == OtaStatus::IDLE);
    REQUIRE(s.rollback_pending == false);
    REQUIRE(std::strlen(s.current_version) > 0);
}

TEST_CASE("OtaInteractor: конструктор — VERIFY_PENDING", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0; val.pending = true;
    OtaInteractor ota(val, ver, [&]{ return now; });
    REQUIRE(ota.status().state == OtaStatus::VERIFY_PENDING);
}

TEST_CASE("OtaInteractor: status() отдаёт живой rollback_pending", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, ver, [&]{ return now; });
    REQUIRE_FALSE(ota.status().rollback_pending);
    val.pending = true;
    REQUIRE(ota.status().rollback_pending);
}

TEST_CASE("OtaInteractor: poll сбрасывает VERIFY_PENDING → IDLE", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0; val.pending = true;
    OtaInteractor ota(val, ver, [&]{ return now; });
    REQUIRE(ota.status().state == OtaStatus::VERIFY_PENDING);
    val.pending = false;
    ota.poll();
    REQUIRE(ota.status().state == OtaStatus::IDLE);
}

TEST_CASE("OtaInteractor: rollback_now → validity.mark_invalid_and_reboot", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, ver, [&]{ return now; });
    ota.rollback_now();
    REQUIRE(val.reboot_called == true);
}

TEST_CASE("OtaInteractor: fetch_versions — кэш 60с, перезапрос после TTL", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    const char* body = R"({"versions":[{"tag":"v0.5.0"}]})";
    ver.next_response = strdup(body);
    OtaInteractor ota(val, ver, [&]{ return now; });
    auto* r1 = ota.fetch_version_list();
    REQUIRE(r1); REQUIRE(std::strstr(r1, "v0.5.0")); free(r1);
    REQUIRE(ver.fetch_count == 1);
    auto* r2 = ota.fetch_version_list(); REQUIRE(r2); free(r2);
    REQUIRE(ver.fetch_count == 1);  // кэш
    now += 60'001;
    auto* r3 = ota.fetch_version_list(); REQUIRE(r3); free(r3);
    REQUIRE(ver.fetch_count == 2);  // перезагрузка после TTL
}

TEST_CASE("OtaInteractor: fetch_versions — nullptr при ошибке", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, ver, [&]{ return now; });
    REQUIRE(ota.fetch_version_list() == nullptr);
}

TEST_CASE("OtaInteractor: lookup_sha256 — находит хеш в кэше", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    ver.next_response = strdup(R"({"versions":[{"tag":"v0.5.0","sha256":"abc123def456"}]})");
    OtaInteractor ota(val, ver, [&]{ return now; });
    auto* r = ota.fetch_version_list(); REQUIRE(r); free(r);
    auto* sha = ota.lookup_sha256("v0.5.0");
    REQUIRE(sha != nullptr);
    REQUIRE(std::strcmp(sha, "abc123def456") == 0);
}

TEST_CASE("OtaInteractor: lookup_sha256 — nullptr если нет", "[ota]") {
    FakeOtaValidity val; FakeOtaVersionIndex ver;
    int64_t now = 0;
    OtaInteractor ota(val, ver, [&]{ return now; });
    REQUIRE(ota.lookup_sha256("v0.5.0") == nullptr);
}
