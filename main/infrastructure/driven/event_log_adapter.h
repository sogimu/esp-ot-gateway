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

#define LOG_RING_SIZE 64

/// Thread-safe ring-buffer event logger — CA implementation.
/// Uses FreeRTOS mutex for cross-core safety.
/// event() is task-context only — no ISR callers, so mutex blocking is safe.
class EventLogAdapter : public ILogger, public IEventLogReader {
public:
    explicit EventLogAdapter(class ITimeSource* time = nullptr);
    ~EventLogAdapter();

    /// Установить источник времени (вызывается после создания SntpTimeAdapter).
    void set_time_source(class ITimeSource* time) { time_ = time; }

    void event(Category cat, const char* fmt, ...) override;

    /// Register a callback for live event streaming (MQTT journal).
    /// Called once during init — no locking needed.
    void set_event_callback(EventAppendCallback cb, void* ctx) override {
        cb_ = cb; cb_ctx_ = ctx;
    }

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
    EventAppendCallback cb_ = nullptr;
    void* cb_ctx_ = nullptr;
};
