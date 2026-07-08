#pragma once

#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/ievent_log_reader.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdarg.h>

struct LogEntry {
    uint32_t time_sec;
    uint8_t  category;
    char     msg[100];
};

#define LOG_RING_SIZE 256

/// Thread-safe ring-buffer event logger — CA implementation.
/// Uses FreeRTOS mutex for cross-core safety.
/// event() is task-context only — no ISR callers, so mutex blocking is safe.
class EventLogAdapter : public ILogger, public IEventLogReader {
public:
    EventLogAdapter();
    ~EventLogAdapter();

    void event(Category cat, const char* fmt, ...) override;

    void set_time_source(class ITimeSource* t) { time_ = t; }

    /// Serialize ring buffer as JSON. Caller MUST hold lock() before calling
    /// and unlock() after httpd_resp_sendstr() completes — protects static buffer.
    const char* to_json();
    void lock();
    void unlock();

    int  get_count() const { return count_; }
    int  get_head()  const { return head_; }

private:
    LogEntry* ring_; // malloc'd (256 × ~108 ≈ 27KB)
    int head_  = 0;
    int count_ = 0;
    class ITimeSource* time_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
};
