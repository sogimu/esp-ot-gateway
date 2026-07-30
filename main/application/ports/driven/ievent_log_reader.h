#pragma once
#include <cstdint>

/// Callback invoked on every event append.
using EventAppendCallback = void (*)(uint8_t category, const char* message,
                                      uint32_t time_sec, bool ts_valid, void* ctx);

/// Read-only access to the event log ring buffer.
/// Implemented by EventLogAdapter on ESP32, by FakeEventLogReader in tests.
///
/// Caller MUST call lock() before reading and unlock() after the response
/// is sent — protects the static JSON buffer from concurrent access.
class IEventLogReader {
public:
    /// Serialize the ring buffer as a JSON string.
    /// Returns pointer to a static buffer (valid until next to_json() call).
    virtual const char* to_json() = 0;

    /// Lock the ring buffer for reading.
    virtual void lock() = 0;
    /// Unlock after the response has been sent.
    virtual void unlock() = 0;

    /// Register a callback for live journal streaming (MQTT).
    virtual void set_event_callback(EventAppendCallback cb, void* ctx) = 0;

    virtual ~IEventLogReader() = default;
};
