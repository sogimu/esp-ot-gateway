#pragma once

#include <stdint.h>

/// Event logger port — ring buffer with category filtering.
class ILogger {
public:
    enum Category {
        SYSTEM = 0,
        USER   = 1,
        EQUIP  = 2,
        MODE   = 3,
        BOOT   = 4
    };

    /// Append a formatted event to the ring buffer.
    virtual void event(Category cat, const char* fmt, ...) = 0;

    virtual ~ILogger() = default;
};

// Legacy aliases
typedef ILogger::Category LogCategory;
constexpr LogCategory LOG_CAT_SYSTEM = ILogger::SYSTEM;
constexpr LogCategory LOG_CAT_USER   = ILogger::USER;
constexpr LogCategory LOG_CAT_EQUIP  = ILogger::EQUIP;
constexpr LogCategory LOG_CAT_MODE   = ILogger::MODE;
constexpr LogCategory LOG_CAT_BOOT   = ILogger::BOOT;
