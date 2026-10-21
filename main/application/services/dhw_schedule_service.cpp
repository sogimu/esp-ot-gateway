#include "application/services/dhw_schedule_service.h"

bool DhwScheduleService::is_allowed(const bool on[24], int hour)
{
    if (hour < 0 || hour >= 24) return true; // invalid/unsynced time → fail-safe (allow)
    return on[hour];
}
