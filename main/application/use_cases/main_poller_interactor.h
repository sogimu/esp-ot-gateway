#pragma once

#include "application/ports/driving/ipollable.h"
#include "application/ports/driving/imain_poller.h"
#include <array>

/// Aggregates IPollable instances and invokes each in sequence.
/// Called from MainPollerTaskAdapter every ~1.1 s.
class MainPollerInteractor : public IMainPoller {
public:
    static constexpr int MAX_POLLABLE = 8;

    MainPollerInteractor();

    /// Register a new pollable. Returns false if full.
    bool add(IPollable* p);

    /// Iterate all registered IPollable and call poll().
    void run_once() override;

private:
    std::array<IPollable*, MAX_POLLABLE> pollables_;
    int count_ = 0;
};
