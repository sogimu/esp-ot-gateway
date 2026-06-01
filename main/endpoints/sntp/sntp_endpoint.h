#pragma once

#include <ctime>
#include <cstring>

class SntpEndpoint {
public:
    SntpEndpoint();
    ~SntpEndpoint();

    void start();
    void stop();

    void set_timezone(int offset_utc);
    void set_servers(const char* srv0, const char* srv1);

    bool is_synced() const;
    bool get_time(struct tm* out) const;

private:
    bool started_;
    int  tz_offset_;
    char srv0_[64];
    char srv1_[64];
};