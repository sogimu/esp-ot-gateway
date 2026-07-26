#pragma once

#include <stddef.h>

/// Aggregates IControlTask instances and invokes each in sequence.
/// Called from ControlLoopTaskAdapter every ~1.1 s.
class IControlLoop {
public:
    virtual void run_once() = 0;
    virtual ~IControlLoop() = default;
};
