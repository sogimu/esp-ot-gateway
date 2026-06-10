#include "application/use_cases/gas_correction_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iconfiguration_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/services/gas_flow_estimator.h"
#include <cstdio>
#include <cmath>
#include <cstring>

GasCorrectionInteractor::GasCorrectionInteractor(IHeatingStateStore& state, IConfigurationStore& config, ILogger& log)
    : state_(state), config_(config), log_(log)
{
    memset(&meter_blob_, 0, sizeof(meter_blob_));
}

void GasCorrectionInteractor::init()
{
    if (config_.load_meter(state_, &meter_blob_)) {
        log_.event(ILogger::SYSTEM, "Журнал сверки: %lu записей",
                   (unsigned long)meter_blob_.corrections_count);
    }
}

void GasCorrectionInteractor::set_k_calib(float v)
{
    if (v < 0.1f) v = 0.1f;
    if (v > 10.0f) v = 10.0f;
    state_.lock_exclusive();
    state_.set_k_calib(v);
    state_.unlock_exclusive();
    config_.save_config(state_);
    log_.event(ILogger::USER, "K_калиб: %.3f", (double)v);
}

void GasCorrectionInteractor::set_p_max(float v)
{
    state_.lock_exclusive();
    state_.set_p_max(v);
    state_.unlock_exclusive();
    config_.save_config(state_);
}

void GasCorrectionInteractor::set_gas_calorific(float v)
{
    state_.lock_exclusive();
    state_.set_gas_calorific(v);
    state_.unlock_exclusive();
    config_.save_config(state_);
}

void GasCorrectionInteractor::set_gas_meter_base(float v)
{
    if (v < 0.0f) v = 0.0f;
    state_.lock_exclusive();
    state_.set_gas_meter_base(v);
    state_.unlock_exclusive();
    meter_blob_.base_reading = v;
    config_.save_meter(state_, &meter_blob_);
    log_.event(ILogger::USER, "База счётчика: %.3f", (double)v);
}

void GasCorrectionInteractor::add_meter_correction(float reading)
{
    if (!gas_flow_) {
        log_.event(ILogger::USER, "Нет gas_flow: сверка отменена");
        return;
    }

    float base = state_.get_gas_meter_base();
    float integral = gas_flow_->integral_m3();
    float estimated = base + integral;

    // Sync base_reading from state into the blob before saving
    meter_blob_.base_reading = base;
    float diff = reading - estimated;

    // Avoid division by zero or tiny values
    if (estimated < 0.001f) {
        log_.event(ILogger::USER, "Объём~0: сверка отменена");
        return;
    }

    float prev_k = state_.get_k_calib();
    float new_k = prev_k * (estimated / reading);
    if (new_k < 0.1f) new_k = 0.1f;
    if (new_k > 10.0f) new_k = 10.0f;

    // Update k_calib
    state_.lock_exclusive();
    state_.set_k_calib(new_k);
    state_.unlock_exclusive();
    config_.save_config(state_);

    // Add entry to correction log (ring buffer)
    int idx = meter_blob_.corrections_head;
    NvsCorrLogEntry& e = meter_blob_.corrections[idx];
    e.timestamp = 0; // Will be set if we have time source; 0 = unknown
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
    config_.save_meter(state_, &meter_blob_);
    config_.save_config(state_);

    log_.event(ILogger::USER,
               "Сверка: K%.2f>%.2f diff=%.1f",
               (double)prev_k, (double)new_k, (double)diff);
}
