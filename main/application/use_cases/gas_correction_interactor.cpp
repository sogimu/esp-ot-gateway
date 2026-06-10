#include "application/use_cases/gas_correction_interactor.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/iconfiguration_store.h"
#include "application/ports/driven/ilogger.h"
#include <cstdio>
#include <cmath>

GasCorrectionInteractor::GasCorrectionInteractor(IHeatingStateStore& state, IConfigurationStore& config, ILogger& log)
    : state_(state), config_(config), log_(log)
{
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
    config_.save_meter(state_);  // "meter" namespace, not "config"
    log_.event(ILogger::USER, "База счётчика: %.3f", (double)v);
}

void GasCorrectionInteractor::add_meter_correction(float reading)
{
    // Strangler pattern: old Controller does the real correction calculation.
    // This interactor will take over in Step 10.
    log_.event(ILogger::USER, "Коррекция счётчика: %.3f", (double)reading);
}
