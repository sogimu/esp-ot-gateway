#pragma once

#include "application/ports/driven/ievent_log_reader.h"
#include <cstring>

/// In-memory event log reader for host-side tests.
class FakeEventLogReader : public IEventLogReader {
public:
    const char* to_json() override {
        return json_;
    }

    void lock() override   { locked_ = true; }
    void unlock() override { locked_ = false; }

    /// Set the JSON to be returned by to_json().
    void set_json(const char* json) {
        strncpy(json_, json, sizeof(json_) - 1);
        json_[sizeof(json_) - 1] = '\0';
    }

    bool is_locked() const { return locked_; }

private:
    char json_[512] = "{\"count\":0,\"events\":[]}";
    bool locked_ = false;
};
