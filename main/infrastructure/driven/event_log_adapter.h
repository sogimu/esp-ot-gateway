#pragma once

#include "application/ports/driven/ilogger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <stdarg.h>

struct LogEntry {
    uint32_t time_sec;
    uint8_t  category;
    char     msg[100];
};

#define LOG_RING_SIZE 512

/// Thread-safe ring-buffer event logger — CA implementation.
/// Uses FreeRTOS spinlock (portMUX_TYPE) for multi-task safety.
/// Critical sections are < 5us — does NOT block OT ISR.
class EventLogAdapter : public ILogger {
public:
    EventLogAdapter();
    ~EventLogAdapter();

    void event(Category cat, const char* fmt, ...) override;

    void set_time_source(class ITimeSource* t) { time_ = t; }

    /// Serialize ring buffer as JSON.
    const char* to_json();

    int  get_count() const { return count_; }
    int  get_head()  const { return head_; }

private:
    LogEntry* ring_; // malloc'd (512 * 88 ≈ 45KB)
    int head_  = 0;
    int count_ = 0;
    class ITimeSource* time_ = nullptr;
    portMUX_TYPE spinlock_ = portMUX_INITIALIZER_UNLOCKED;
};
