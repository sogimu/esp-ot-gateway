#pragma once

#include "application/ports/driven/itime_source.h"

/// ITimeSource adapter wrapping esp_timer_get_time().
class EspTimerAdapter : public ITimeSource {
public:
    uint64_t now_us() const override;
    uint32_t now_sec() const override;
    void set_timezone(int) override {}  // no-op: wall-clock TZ handled by SntpTimeAdapter
};
