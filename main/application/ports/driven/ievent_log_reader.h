#pragma once

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

    virtual ~IEventLogReader() = default;
};
