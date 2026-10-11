#include "application/services/ch_hysteresis_service.h"

bool CHHysteresisService::should_heat(float ch_flow, float ch_setpoint, float hysteresis, bool ch_enabled)
{
    if (!ch_enabled) return false;
    return ch_flow < (ch_setpoint - hysteresis);
}

bool CHHysteresisService::should_stop(float ch_flow, float ch_setpoint)
{
    return ch_flow >= ch_setpoint;
}
