#pragma once

#include "application/ports/driving/icontrol_task.h"
#include "application/ports/driving/icontrol_loop.h"
#include <vector>

/// Aggregates IControlTask instances and invokes each in sequence.
/// Called from ControlLoopTaskAdapter every ~1.1 s.
class ControlLoopInteractor : public IControlLoop {
public:
    ControlLoopInteractor();

    /// Register a new control task. Always succeeds.
    bool add(IControlTask* p);

    /// Iterate all registered IControlTask and call execute().
    void run_once() override;

private:
    std::vector<IControlTask*> tasks_;
};
