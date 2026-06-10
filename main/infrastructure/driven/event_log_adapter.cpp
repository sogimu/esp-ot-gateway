#include "infrastructure/driven/event_log_adapter.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include <cstdlib>

EventLogAdapter::EventLogAdapter()
{
    ring_ = static_cast<LogEntry*>(malloc(LOG_RING_SIZE * sizeof(LogEntry)));
    if (ring_) std::memset(ring_, 0, LOG_RING_SIZE * sizeof(LogEntry));
}

EventLogAdapter::~EventLogAdapter()
{
    free(ring_);
}

void EventLogAdapter::event(Category cat, const char* fmt, ...)
{
    if (!ring_) return;

    time_t now;
    time(&now);
    uint32_t ts = static_cast<uint32_t>(now > 0 ? now : 0);

    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Write entry FIRST, then advance head_ — ensures to_json() never sees torn entry.
    // Spinlock protects head_/count_ from concurrent event() in other task.
    portENTER_CRITICAL(&spinlock_);
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
    portEXIT_CRITICAL(&spinlock_);
}

const char* EventLogAdapter::to_json()
{
    if (!ring_) return "{\"count\":0,\"events\":[]}";

    // Snapshot head/count under spinlock — formatting happens WITHOUT lock
    // (formatting is slow, must not block OT ISR)
    int head_snap, count_snap;
    portENTER_CRITICAL(&spinlock_);
    head_snap = head_;
    count_snap = count_;
    portEXIT_CRITICAL(&spinlock_);

    static char buf[24576];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"count\":%d,\"events\":[", count_snap);

    int start = (count_snap < LOG_RING_SIZE) ? 0 : head_snap;
    int total = count_snap;

    int last_yday = -1;  // track day changes for date markers
    bool first = true;    // comma separator control

    for (int i = 0; i < total && pos < (int)sizeof(buf) - 128; i++) {
        int idx = (start + i) % LOG_RING_SIZE;
        const LogEntry& e = ring_[idx];

        struct tm ti;
        char tbuf[16] = "??:??:??";
        int yday = -1;
        if (e.time_sec > 0) {
            time_t t = (time_t)e.time_sec;
            localtime_r(&t, &ti);
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            yday = ti.tm_yday;
        }

        // Emit date marker when day changes (or first valid event)
        if (yday >= 0 && yday != last_yday) {
            char datebuf[32];
            snprintf(datebuf, sizeof(datebuf), "%02d.%02d.%04d",
                     ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
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
    return buf;
}
