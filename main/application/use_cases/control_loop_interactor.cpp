#include "application/use_cases/control_loop_interactor.h"

ControlLoopInteractor::ControlLoopInteractor()
{
    pollables_.fill(nullptr);
}

bool ControlLoopInteractor::add(IControlTask* p)
{
    if (count_ >= MAX_POLLABLE) return false;
    pollables_[count_++] = p;
    return true;
}

void ControlLoopInteractor::run_once()
{
    for (int i = 0; i < count_; i++) {
        if (pollables_[i]) {
            pollables_[i]->execute();
        }
    }
}
