/// Stub for BoilerPollInteractor methods called by SystemConfigInteractor.
/// Avoids pulling in the full boiler_poll_interactor.cpp (depends on ESP-IDF).

#include "application/use_cases/boiler_poll_interactor.h"

void BoilerPollInteractor::set_ch_enable(bool) {}
void BoilerPollInteractor::set_dhw_enable(bool) {}
void BoilerPollInteractor::set_ch_setpoint(float) {}
void BoilerPollInteractor::set_dhw_setpoint(float) {}
void BoilerPollInteractor::set_dhw_hysteresis(float) {}
void BoilerPollInteractor::trigger_fault_reset() {}
void BoilerPollInteractor::execute() {}
