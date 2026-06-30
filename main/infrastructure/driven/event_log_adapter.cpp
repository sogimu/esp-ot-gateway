#include "infrastructure/driven/event_log_adapter.h"
#include "application/ports/driven/itime_source.h"

#include <cstdio>
#include <cstring>
#include <chrono>

namespace {
struct ChronoDate { int year, mon, day, hour, min, sec; };
ChronoDate civil_from_seconds(int64_t secs) {
    int64_t z = secs / 86400 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    int64_t mp = (5*doy + 2) / 153;
    int d = doy - (153*mp + 2)/5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    int64_t sod = secs % 86400;
    if (sod < 0) sod += 86400;
    return {static_cast<int>(y), m, d, static_cast<int>(sod/3600), static_cast<int>((sod%3600)/60), static_cast<int>(sod%60)};
}
}
#include <cstdarg>
#include <cstdlib>

EventLogAdapter::EventLogAdapter()
{
    ring_ = static_cast<LogEntry*>(malloc(LOG_RING_SIZE * sizeof(LogEntry)));
    if (ring_) std::memset(ring_, 0, LOG_RING_SIZE * sizeof(LogEntry));
    mutex_ = xSemaphoreCreateMutex();
}

EventLogAdapter::~EventLogAdapter()
{
    free(ring_);
    if (mutex_) vSemaphoreDelete(mutex_);
}

void EventLogAdapter::event(Category cat, const char* fmt, ...)
{
    if (!ring_) return;

    uint32_t ts = time_ ? time_->now_s() : 0;

    char buf[100];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Mutex protects ring buffer from concurrent to_json() reads.
    // event() is task-context only (no ISR callers), so blocking is safe.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    int idx = head_;
    LogEntry& e = ring_[idx];
    e.time_sec = ts;
    e.category = static_cast<uint8_t>(cat);
    size_t n = 0;
    for (const char* s = buf; *s && n < sizeof(e.msg) - 1; s++, n++)
        e.msg[n] = *s;
    e.msg[n] = '\0';
    // Advance AFTER write — readers see stable entries
    head_ = (head_ + 1) % LOG_RING_SIZE;
    if (count_ < LOG_RING_SIZE) count_++;
    xSemaphoreGive(mutex_);
}

const char* EventLogAdapter::to_json()
{
    if (!ring_) return "{\"count\":0,\"events\":[]}";

    // Hold mutex for entire formatting — prevents event() from
    // overwriting entries while we read them (cross-core race).
    xSemaphoreTake(mutex_, portMAX_DELAY);
    int head_snap = head_;
    int count_snap = count_;
    // Snapshot is now stable — entries won't be overwritten while we format
    // because event() waits on the mutex.

    static char buf[65536];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"count\":%d,\"events\":[", count_snap);

    int start = (count_snap < LOG_RING_SIZE) ? 0 : head_snap;
    int total = count_snap;

    int last_yday = -1;  // track day changes for date markers
    bool first = true;    // comma separator control

    for (int i = 0; i < total && pos < (int)sizeof(buf) - 128; i++) {
        int idx = (start + i) % LOG_RING_SIZE;
        const LogEntry& e = ring_[idx];

        char tbuf[16] = "??:??:??";
        // Compute yday for date separators (simplified: use day-of-year from civil date)
        int yday = -1;
        if (e.time_sec > 0) {
            int64_t local_secs = e.time_sec + time_->tz_offset() * 3600LL;
            auto cd = civil_from_seconds(local_secs);
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", cd.hour, cd.min, cd.sec);
            // Approximate yday from month/day
            int mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
            yday = mdays[cd.mon - 1] + cd.day - 1;
            if (cd.mon > 2 && (cd.year % 4 == 0 && (cd.year % 100 != 0 || cd.year % 400 == 0)))
                yday++;
        }

        // Emit date marker when day changes (or first valid event)
        if (yday >= 0 && yday != last_yday) {
            char datebuf[32];
            if (e.time_sec > 0) {
                int64_t local_secs = e.time_sec + time_->tz_offset() * 3600LL;
                auto cd = civil_from_seconds(local_secs);
                snprintf(datebuf, sizeof(datebuf), "%02d.%02d.%04d",
                         cd.day, cd.mon, cd.year);
            } else {
                snprintf(datebuf, sizeof(datebuf), "??.??.????");
            }
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "%s{\"date\":\"%s\"}",
                            first ? "" : ",", datebuf);
            first = false;
            last_yday = yday;
        }

        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"t\":\"%s\",\"c\":%d,\"m\":\"",
                        first ? "" : ",", tbuf, e.category);
        first = false;

        for (const char* s = e.msg; *s && pos < (int)sizeof(buf) - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            buf[pos++] = *s;
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"}");
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    xSemaphoreGive(mutex_);
    return buf;
}
