#pragma once

#include "application/ports/driving/icontrol_task.h"
#include "application/ports/driving/icontrol_loop.h"
#include <array>

/// Aggregates IControlTask instances and invokes each in sequence.
/// Called from ControlLoopTaskAdapter every ~1.1 s.
class ControlLoopInteractor : public IControlLoop {
public:
    static constexpr int MAX_POLLABLE = 12;

    ControlLoopInteractor();

    /// Register a new pollable. Returns false if full.
    bool add(IControlTask* p);

    /// Iterate all registered IControlTask and call poll().
    void run_once() override;

private:
    std::array<IControlTask*, MAX_POLLABLE> pollables_;
    int count_ = 0;
};
