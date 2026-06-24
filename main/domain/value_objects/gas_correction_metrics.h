#pragma once
#include <cmath>

/// Результат сравнения двух последовательных сверок газового счётчика.
/// Чистый доменный value object — не зависит ни от NVS, ни от JSON, ни от UI.
struct GasCorrectionMetrics {
    float actual_consumed    = 0;  // м³ — реальное потребление за период
    float estimated_consumed = 0;  // м³ — оценённое потребление за период
    float error_pct          = 0;  // %   — |оценка − факт| / факт × 100

    /// Рекомендованный корректирующий множитель.
    /// k_calib_new = k_calib_prev × k_factor()
    /// > 1 — модель занижает расход, нужно увеличить k
    /// < 1 — модель завышает расход, нужно уменьшить k
    /// = 1 — менять не нужно (или недостаточно данных)
    float k_factor() const {
        if (estimated_consumed < 0.001f || actual_consumed <= 0.0f) return 1.0f;
        return actual_consumed / estimated_consumed;
    }
};

/// Вычислить метрики по двум последовательным сверкам.
///
/// Параметры:
///   prev_actual     — показание счётчика на предыдущей сверке
///   prev_estimated  — расчётный объём на предыдущей сверке (estimated_total)
///   last_actual     — показание счётчика на последней сверке
///   last_estimated  — расчётный объём на последней сверке
///
/// После каждой сверки integral сбрасывается в 0, а gas_meter_base
/// устанавливается равным фактическому показанию. Поэтому:
///   actual_consumed    = last_actual - prev_actual
///   estimated_consumed = last_estimated - prev_actual
///                      (= накопленный integral между сверками)
inline GasCorrectionMetrics compute_correction_metrics(
    float prev_actual, float prev_estimated,
    float last_actual, float last_estimated)
{
    GasCorrectionMetrics m;
    m.actual_consumed    = last_actual  - prev_actual;
    m.estimated_consumed = last_estimated - prev_actual;
    if (m.actual_consumed > 0.001f)
        m.error_pct = fabsf(m.actual_consumed - m.estimated_consumed)
                     / m.actual_consumed * 100.0f;
    return m;
}
