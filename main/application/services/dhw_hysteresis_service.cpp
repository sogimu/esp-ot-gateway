#include "application/services/dhw_hysteresis_service.h"

bool DHWHysteresisService::should_heat(float dhw_temp, float dhw_setpoint, float hysteresis, bool dhw_enabled)
{
    if (!dhw_enabled) return false;
    return dhw_temp < (dhw_setpoint - hysteresis);
}

bool DHWHysteresisService::should_stop(float dhw_temp, float dhw_setpoint)
{
    return dhw_temp >= dhw_setpoint;
}
