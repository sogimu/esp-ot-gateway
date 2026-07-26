#include "application/use_cases/sensors_poll_interactor.h"
#include "application/ports/driven/itemperature_sensor.h"
#include "application/ports/driven/iheating_state_store.h"

SensorsPollInteractor::SensorsPollInteractor(ITemperatureSensor& sensor, IHeatingStateStore& state)
    : sensor_(sensor), state_(state)
{
}

void SensorsPollInteractor::execute()
{
    // Call sensors_poll() every cycle — it manages its own 2-phase state machine
    sensor_.request_conversion();

    auto r0 = sensor_.read_sensor(0);
    if (r0.valid) {
        state_.lock_exclusive();
        state_.set_t1_temp(r0.temperature);
        state_.unlock_exclusive();
    }

    auto r1 = sensor_.read_sensor(1);
    if (r1.valid) {
        state_.lock_exclusive();
        state_.set_t2_temp(r1.temperature);
        state_.unlock_exclusive();
    }
}
