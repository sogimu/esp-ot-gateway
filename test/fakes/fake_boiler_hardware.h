#pragma once

#include "application/ports/driven/iboiler_hardware.h"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>

/// Fake boiler hardware — programmable responses for unit testing.
class FakeBoilerHardware : public IBoilerHardware {
public:
    FakeBoilerHardware() { clear(); }

    void clear() {
        connected_ = true;
        read_responses_.clear();
        write_calls_.clear();
        status_flags_ = 0;
        fault_reset_called_ = false;
    }

    ReadResult read(uint8_t data_id) override {
        auto it = read_responses_.find(data_id);
        if (it != read_responses_.end()) {
            return {true, it->second};
        }
        return {false, 0};
    }

    ReadResult read_status(uint8_t master_flags, bool fault_reset) override {
        status_master_ = master_flags;
        if (fault_reset) fault_reset_called_ = true;
        auto it = read_responses_.find(0); // ID 0 = status
        if (it != read_responses_.end()) {
            return {true, it->second};
        }
        return {false, 0};
    }

    WriteResult write(uint8_t data_id, uint16_t value) override {
        write_calls_.push_back({data_id, value, false});
        return {true};
    }

    WriteResult write_status(uint8_t master_flags, bool fault_reset) override {
        status_master_ = master_flags;
        if (fault_reset) fault_reset_called_ = true;
        write_calls_.push_back({0, 0, true});
        return {true};
    }

    bool is_connected() const override { return connected_; }

    /// Set what read() returns for a given data_id.
    void set_read_response(uint8_t data_id, float value_f88) {
        read_responses_[data_id] = value_f88;
    }

    /// Simulate disconnection.
    void set_connected(bool v) { connected_ = v; }

    // ── Call tracking for test assertions ─────────────────
    struct WriteCall {
        uint8_t data_id;
        uint16_t value;
        bool is_status;
    };

    std::unordered_map<uint8_t, float> read_responses_;
    std::vector<WriteCall> write_calls_;
    uint8_t status_master_ = 0;
    bool fault_reset_called_ = false;
    bool connected_ = true;
};
