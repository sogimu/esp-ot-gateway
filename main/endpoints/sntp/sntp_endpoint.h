#pragma once

#include <ctime>

class SntpEndpoint {
public:
    SntpEndpoint();
    ~SntpEndpoint();

    void start();
    void stop();

    void set_timezone(int offset_utc);

    bool is_synced() const;
    bool get_time(struct tm* out) const;

private:
    bool started_;
    int  tz_offset_;
};