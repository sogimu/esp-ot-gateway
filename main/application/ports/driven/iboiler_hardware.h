#pragma once

#include <stdint.h>
#include <stddef.h>

/// Boiler hardware port — OpenTherm data reads/writes.
/// Abstracts the protocol so use cases don't depend on OT frame details.
class IBoilerHardware {
public:
    struct ReadResult  { bool success; float value_f88; };
    struct WriteResult { bool success; };

    virtual ReadResult  read(uint8_t data_id) = 0;
    virtual ReadResult  read_status(uint8_t master_flags, bool fault_reset) = 0;
    virtual WriteResult write(uint8_t data_id, uint16_t value) = 0;
    virtual WriteResult write_status(uint8_t master_flags, bool fault_reset) = 0;
    virtual bool        is_connected() const = 0;
    virtual ~IBoilerHardware() = default;
};
