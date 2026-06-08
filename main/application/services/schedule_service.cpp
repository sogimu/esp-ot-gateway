#include "application/services/schedule_service.h"

ScheduleService::Result ScheduleService::evaluate(bool schedule_enabled, const float temps[24], int current_hour)
{
    Result r = {false, 30.0f};
    if (!schedule_enabled) return r;
    if (current_hour < 0 || current_hour >= 24) return r;

    float sp = temps[current_hour];
    if (!is_valid_setpoint(sp)) return r;

    r.has_setpoint = true;
    r.setpoint = sp;
    return r;
}

bool ScheduleService::is_valid_setpoint(float sp)
{
    return sp >= 20.0f && sp <= 80.0f;
}
