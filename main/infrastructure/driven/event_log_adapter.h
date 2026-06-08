#pragma once

#include "application/ports/driven/ilogger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdint.h>
#include <stdarg.h>

struct LogEntry {
    uint32_t time_sec;
    uint8_t  category;
    char     msg[48];
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

    /// Serialize ring buffer as JSON.
    const char* to_json();

    int  get_count() const { return count_; }
    int  get_head()  const { return head_; }

private:
    LogEntry* ring_; // malloc'd (512 * 56 = 28KB)
    int head_  = 0;
    int count_ = 0;
    portMUX_TYPE spinlock_ = portMUX_INITIALIZER_UNLOCKED;
};
