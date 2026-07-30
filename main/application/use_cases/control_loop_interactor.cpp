#include "application/use_cases/control_loop_interactor.h"

bool ControlLoopInteractor::add(IControlTask* p)
{
    tasks_.push_back(p);
    return true;
}

void ControlLoopInteractor::run_once()
{
    for (auto* task : tasks_) {
        if (task) task->execute();
    }
}
