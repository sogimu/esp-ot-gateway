#pragma once

#include <stddef.h>

/// Aggregates IPollable instances and invokes each in sequence.
/// Called from MainPollerTaskAdapter every ~1.1 s.
class IMainPoller {
public:
    virtual void run_once() = 0;
    virtual ~IMainPoller() = default;
};
