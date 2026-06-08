#include "infrastructure/driven/esp_timer_adapter.h"
#include "esp_timer.h"
#include <ctime>

uint64_t EspTimerAdapter::now_us() const
{
    return esp_timer_get_time();
}

uint32_t EspTimerAdapter::now_sec() const
{
    time_t now;
    time(&now);
    return static_cast<uint32_t>(now > 0 ? now : 0);
}
