#include "application/use_cases/main_poller_interactor.h"

MainPollerInteractor::MainPollerInteractor()
{
    pollables_.fill(nullptr);
}

bool MainPollerInteractor::add(IPollable* p)
{
    if (count_ >= MAX_POLLABLE) return false;
    pollables_[count_++] = p;
    return true;
}

void MainPollerInteractor::run_once()
{
    for (int i = 0; i < count_; i++) {
        if (pollables_[i]) {
            pollables_[i]->poll();
        }
    }
}
