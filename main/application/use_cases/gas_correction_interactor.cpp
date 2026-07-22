#include "application/use_cases/gas_correction_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/igas_correction_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include "application/services/gas_flow_estimator.h"
#include "domain/value_objects/gas_correction_metrics.h"
#include <cstdio>
#include <cmath>
#include <cstring>

GasCorrectionInteractor::GasCorrectionInteractor(IHeatingStateStore& state, IGasCorrectionStore& store, ILogger& log)
    : state_(state), store_(store), log_(log)
{
    memset(&meter_blob_, 0, sizeof(meter_blob_));
}

void GasCorrectionInteractor::init()
{
    if (store_.load_meter(state_, &meter_blob_)) {
        log_.event(ILogger::SYSTEM, "Журнал сверки: %lu записей",
                   (unsigned long)meter_blob_.corrections_count);
    }
    kalman_k_.reset(state_.get_k_calib());
    // Sync gas_flow with persisted k_calib (survives reboots)
    if (gas_flow_) gas_flow_->set_k_calib(state_.get_k_calib());
}

void GasCorrectionInteractor::set_k_calib(float v)
{
    if (v < 0.1f) v = 0.1f;
    if (v > 10.0f) v = 10.0f;
    state_.lock_exclusive();
    state_.set_k_calib(v);
    state_.unlock_exclusive();
    if (gas_flow_) gas_flow_->set_k_calib(v);
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "K_калиб: %.3f", (double)v);
}

void GasCorrectionInteractor::set_p_max(float v)
{
    state_.lock_exclusive();
    state_.set_p_max(v);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
}

void GasCorrectionInteractor::set_gas_calorific(float v)
{
    state_.lock_exclusive();
    state_.set_gas_calorific(v);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
}

void GasCorrectionInteractor::set_gas_meter_base(float v)
{
    if (v < 0.0f) v = 0.0f;
    state_.lock_exclusive();
    state_.set_gas_meter_base(v);
    state_.unlock_exclusive();
    meter_blob_.base_reading = v;
    store_.save_meter(state_, &meter_blob_);
    log_.event(ILogger::USER, "База счётчика: %.3f", (double)v);
}

bool GasCorrectionInteractor::add_meter_correction(float reading)
{
    if (!gas_flow_) {
        log_.event(ILogger::USER, "Нет gas_flow: сверка отменена");
        return false;
    }

    float base = state_.get_gas_meter_base();
    float integral = gas_flow_->integral_m3();
    float estimated = base + integral;

    // Sync base_reading from state into the blob before saving
    meter_blob_.base_reading = base;
    float diff = reading - estimated;

    // Reject correction if estimated consumption is too small
    // compared to actual — guards against integral loss from reboots
    float actual_consumed = reading - base;
    if (actual_consumed < 0.01f) {
        log_.event(ILogger::USER, "Расход < 0.01 м³: сверка отменена");
        return false;
    }
    if (integral < actual_consumed * 0.1f) {
        log_.event(ILogger::USER, "Интеграл (%.3f) < 10%% расхода (%.3f): сверка отменена",
                   (double)integral, (double)actual_consumed);
        return false;
    }

    float prev_k = state_.get_k_calib();
    auto m = compute_correction_metrics(base, base, reading, estimated);
    float raw_k = prev_k * m.k_factor();
    // Clamp raw correction factor to prevent extreme swings from tiny samples
    if (raw_k < 0.5f) raw_k = 0.5f;
    if (raw_k > 2.0f) raw_k = 2.0f;
    float new_k = kalman_k_.update(raw_k);
    if (new_k < 0.1f) new_k = 0.1f;
    if (new_k > 10.0f) new_k = 10.0f;

    // Update k_calib
    state_.lock_exclusive();
    state_.set_k_calib(new_k);
    state_.unlock_exclusive();
    if (gas_flow_) gas_flow_->set_k_calib(new_k);
    store_.save_boiler_config(state_);

    // Add entry to correction log (ring buffer)
    int idx = meter_blob_.corrections_head;
    NvsCorrLogEntry& e = meter_blob_.corrections[idx];
    e.timestamp = time_ ? time_->now_s() : 0;
    e.actual_reading = reading;
    e.estimated_total = estimated;
    e.difference = diff;
    e.prev_k_calib = prev_k;
    e.new_k_calib = new_k;

    // Advance head (ring buffer)
    meter_blob_.corrections_head = (idx + 1) % CORRECTION_LOG_SIZE;
    if (meter_blob_.corrections_count < CORRECTION_LOG_SIZE)
        meter_blob_.corrections_count++;

    // Track last correction metadata
    meter_blob_.last_correction_actual = reading;
    meter_blob_.integral_at_last_correction = integral;

    // Persist
    store_.save_meter(state_, &meter_blob_);
    store_.save_boiler_config(state_);

    // Sync: reset integral and set base to actual reading
    // so "Текущее расчётное" == "Показание счётчика" after correction
    if (gas_flow_) {
        gas_flow_->set_integral(0);
        store_.save_integral(0);  // persist immediately so reboot doesn't restore old value
    }
    state_.lock_exclusive();
    state_.set_gas_meter_base(reading);
    state_.unlock_exclusive();
    meter_blob_.base_reading = reading;
    store_.save_meter(state_, &meter_blob_);

    log_.event(ILogger::USER,
               "Сверка: K%.4f>%.4f откл=%.3f",
               (double)prev_k, (double)new_k, (double)diff);
    return true;
}

void GasCorrectionInteractor::reset_corrections()
{
    // Clear correction log ring buffer
    memset(&meter_blob_, 0, sizeof(meter_blob_));
    meter_blob_.base_reading = state_.get_gas_meter_base();
    // Reset k_calib to 1.0
    state_.lock_exclusive();
    state_.set_k_calib(1.0f);
    state_.unlock_exclusive();
    kalman_k_.reset(1.0f);
    if (gas_flow_) gas_flow_->set_k_calib(1.0f);
    store_.save_meter(state_, &meter_blob_);
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "Журнал сверки и K сброшены");
}

void GasCorrectionInteractor::set_gas_temp_offset(float v)
{
    if (v < -20.0f) v = -20.0f;
    if (v >  10.0f) v =  10.0f;
    state_.lock_exclusive();
    state_.set_gas_temp_offset(v);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "Δt газа: %.1f °C", (double)v);
}

void GasCorrectionInteractor::set_ch_power(float pmin, float pmax)
{
    if (pmin < 1.0f)  pmin = 1.0f;
    if (pmin > 15.0f) pmin = 15.0f;
    if (pmax < 10.0f) pmax = 10.0f;
    if (pmax > 40.0f) pmax = 40.0f;
    if (pmin >= pmax) { pmin = 5.5f; pmax = 24.0f; }
    state_.lock_exclusive();
    state_.set_ch_pmin(pmin);
    state_.set_ch_pmax(pmax);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "CH мощность: %.1f–%.1f кВт (входная)",
               (double)pmin, (double)pmax);
}

void GasCorrectionInteractor::set_dhw_power(float pmin, float pmax)
{
    if (pmin < 1.0f)  pmin = 1.0f;
    if (pmin > 15.0f) pmin = 15.0f;
    if (pmax < 10.0f) pmax = 10.0f;
    if (pmax > 40.0f) pmax = 40.0f;
    if (pmin >= pmax) { pmin = 5.5f; pmax = 24.0f; }
    state_.lock_exclusive();
    state_.set_dhw_pmin(pmin);
    state_.set_dhw_pmax(pmax);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "ГВС мощность: %.1f–%.1f кВт", (double)pmin, (double)pmax);
}

void GasCorrectionInteractor::set_efficiency_points(float t1, float v1,
                                                      float t2, float v2,
                                                      float t3, float v3)
{
    if (t1 < 20.0f) t1 = 20.0f;
    if (t1 > 90.0f) t1 = 90.0f;
    if (t2 < 20.0f) t2 = 20.0f;
    if (t2 > 90.0f) t2 = 90.0f;
    if (t3 < 20.0f) t3 = 20.0f;
    if (t3 > 90.0f) t3 = 90.0f;
    if (v1 < 0.80f) v1 = 0.80f;
    if (v1 > 1.00f) v1 = 1.00f;
    if (v2 < 0.80f) v2 = 0.80f;
    if (v2 > 1.00f) v2 = 1.00f;
    if (v3 < 0.80f) v3 = 0.80f;
    if (v3 > 1.00f) v3 = 1.00f;
    if (t1 >= t2) { t1 = 30.0f; t2 = 55.0f; }
    if (t2 >= t3) { t2 = 55.0f; t3 = 80.0f; }
    state_.lock_exclusive();
    state_.set_eff_t1(t1); state_.set_eff_v1(v1);
    state_.set_eff_t2(t2); state_.set_eff_v2(v2);
    state_.set_eff_t3(t3); state_.set_eff_v3(v3);
    state_.unlock_exclusive();
    store_.save_boiler_config(state_);
    log_.event(ILogger::USER, "КПД точки: (%.0f,%.3f) (%.0f,%.3f) (%.0f,%.3f)",
               (double)t1, (double)v1, (double)t2, (double)v2, (double)t3, (double)v3);
}
