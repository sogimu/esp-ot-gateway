#include "application/services/dhw_schedule_service.h"

bool DhwScheduleService::is_allowed(bool schedule_enabled, const bool on[24], int hour)
{
    if (!schedule_enabled) return true;      // no schedule → heating allowed
    if (hour < 0 || hour >= 24) return true; // invalid/unsynced time → fail-safe (allow)
    return on[hour];
}
