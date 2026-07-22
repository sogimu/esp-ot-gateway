#include "application/ports/driven/igas_correction_store.h"
/// Unit tests for domain/value_objects/gas_correction_metrics.h
/// Covers: compute_correction_metrics() and GasCorrectionMetrics::k_factor()
/// These are PURE functions (no dependencies) — no mocks needed.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/value_objects/gas_correction_metrics.h"

using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// compute_correction_metrics — базовые сценарии
// ═══════════════════════════════════════════════════════════════

struct FakeGasStore : IGasCorrectionStore {
    bool load_meter(IHeatingStateStore&, void*) override { return false; }
    void save_meter(const IHeatingStateStore&, const void*) override {}
    void save_integral(float) override {}
    void save_boiler_config(const IHeatingStateStore&) override {}
};

TEST_CASE("correction metrics: 50% underestimation", "[domain][gas_metrics]")
{
    // prev: факт=100, расчёт=100 (идеальная сверка, integral был 0)
    // last: факт=200, расчёт=150 (накопилось 50 вместо 100)
    // → потребление факт=100, оценка=50 → ошибка 50%, k=2.0
    auto m = compute_correction_metrics(100, 100, 200, 150);

    CHECK(m.actual_consumed    == Approx(100.0f));
    CHECK(m.estimated_consumed == Approx(50.0f));
    CHECK(m.error_pct          == Approx(50.0f).margin(0.01f));
    CHECK(m.k_factor()         == Approx(2.0f).margin(0.01f));
}

TEST_CASE("correction metrics: perfect estimate", "[domain][gas_metrics]")
{
    auto m = compute_correction_metrics(100, 100, 110, 110);

    CHECK(m.actual_consumed    == Approx(10.0f));
    CHECK(m.estimated_consumed == Approx(10.0f));
    CHECK(m.error_pct          == Approx(0.0f));
    CHECK(m.k_factor()         == Approx(1.0f));
}

TEST_CASE("correction metrics: overestimation", "[domain][gas_metrics]")
{
    // Модель завысила: насчитала 130, реально 120 (потребление 20)
    // estimated_consumed=30, actual_consumed=20 → ошибка = |20-30|/20*100 = 50%
    // k_factor = 20/30 = 0.667
    auto m = compute_correction_metrics(100, 100, 120, 130);

    CHECK(m.actual_consumed    == Approx(20.0f));
    CHECK(m.estimated_consumed == Approx(30.0f));
    CHECK(m.error_pct          == Approx(50.0f).margin(0.01f));
    CHECK(m.k_factor()         == Approx(0.6667f).margin(0.01f));
}

TEST_CASE("correction metrics: zero consumption — защита от деления на 0", "[domain][gas_metrics]")
{
    auto m = compute_correction_metrics(100, 100, 100, 100);

    CHECK(m.actual_consumed    == Approx(0.0f));
    CHECK(m.estimated_consumed == Approx(0.0f));
    CHECK(m.error_pct          == Approx(0.0f));
    CHECK(m.k_factor()         == Approx(1.0f)); // не меняем k
}

TEST_CASE("correction metrics: handles reading going backwards", "[domain][gas_metrics]")
{
    // prev.actual=150, last.actual=140 (пользователь ошибся при вводе?)
    auto m = compute_correction_metrics(150, 150, 140, 140);

    CHECK(m.actual_consumed    == Approx(-10.0f));
    CHECK(m.estimated_consumed == Approx(-10.0f));
    CHECK(m.error_pct          == Approx(0.0f));   // error_pct не вычисляется при actual_consumed ≤ 0
    CHECK(m.k_factor()         == Approx(1.0f));   // k не меняется
}

// ═══════════════════════════════════════════════════════════════
// k_factor — граничные условия
// ═══════════════════════════════════════════════════════════════

TEST_CASE("k_factor: zero estimated_consumed returns 1.0", "[domain][gas_metrics]")
{
    GasCorrectionMetrics m;
    m.actual_consumed    = 100;
    m.estimated_consumed = 0;
    CHECK(m.k_factor() == Approx(1.0f));
}

TEST_CASE("k_factor: tiny estimated_consumed returns 1.0", "[domain][gas_metrics]")
{
    GasCorrectionMetrics m;
    m.actual_consumed    = 100;
    m.estimated_consumed = 0.0005f; // < 0.001 порога
    CHECK(m.k_factor() == Approx(1.0f));
}

TEST_CASE("k_factor: negative actual_consumed returns 1.0", "[domain][gas_metrics]")
{
    GasCorrectionMetrics m;
    m.actual_consumed    = -5;
    m.estimated_consumed = 10;
    CHECK(m.k_factor() == Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Реальные данные из логов (Томск, Baxi Duo-tec, июнь 2026)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("correction metrics: replicate real Tomsk data [1→2]", "[domain][gas_metrics]")
{
    // Коррекция [1]: 17.06 08:37, факт=1340.944, расчёт=1340.797
    // Коррекция [2]: 19.06 01:07, факт=1344.639, расчёт=1343.559
    // Ожидаемая погрешность: |1.080| / 3.695 * 100 = 29.2%
    auto m = compute_correction_metrics(1340.944f, 1340.797f, 1344.639f, 1343.559f);

    CHECK(m.actual_consumed    == Approx(3.695f).margin(0.001f));
    CHECK(m.estimated_consumed == Approx(2.615f).margin(0.001f));
    CHECK(m.error_pct          == Approx(29.2f).margin(0.1f));
    CHECK(m.k_factor()         == Approx(1.413f).margin(0.01f));
}
