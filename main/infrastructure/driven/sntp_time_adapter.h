#pragma once

#include "application/ports/driven/itime_source.h"

class ILogger;

/// Standalone ITimeSource — NTP-synced wall clock via lwip SNTP.
class SntpTimeAdapter : public ITimeSource {
public:
    SntpTimeAdapter();
    ~SntpTimeAdapter();

    void start();
    void set_timezone(int tz_offset) override;
    void set_servers(const char* srv0, const char* srv1);
    void set_logger(ILogger* log) { logger_ = log; }

    uint64_t now_us() const override;
    uint32_t now_sec() const override;

private:
    int  tz_offset_ = 3;
    char srv0_[64] = {};
    char srv1_[64] = {};
    bool started_ = false;
    ILogger* logger_ = nullptr;
    static const char* TAG;
};
