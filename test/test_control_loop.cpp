/// Tests for ControlLoopInteractor (application/use_cases/control_loop_interactor.h)
/// Covers: adding pollables, run_once iteration, ordering.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/control_loop_interactor.h"
#include <vector>

using Catch::Approx;

/// A simple spy IControlTask that counts how many times it was polled.
class SpyPollable : public IControlTask {
public:
    explicit SpyPollable(const char* label = "") : label_(label) {}

    void execute() override {
        call_count_++;
        last_order_ = global_counter_++;
    }

    void reset() {
        call_count_ = 0;
        last_order_ = -1;
        global_counter_ = 0;
    }

    const char* label_ = "";
    int call_count_ = 0;
    int last_order_ = -1;
    static int global_counter_;
};

int SpyPollable::global_counter_ = 0;

// ── Core functionality ──────────────────────────────────────

TEST_CASE("MainPoller: empty poller does nothing", "[poller][app]") {
    ControlLoopInteractor poller;
    poller.run_once(); // should not crash
}

TEST_CASE("MainPoller: single pollable is called", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable spy;
    spy.reset();

    poller.add(&spy);
    poller.run_once();

    REQUIRE(spy.call_count_ == 1);
}

TEST_CASE("MainPoller: all pollables are called on each run_once", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable a, b, c;
    a.reset(); b.reset(); c.reset();

    poller.add(&a);
    poller.add(&b);
    poller.add(&c);

    poller.run_once();

    REQUIRE(a.call_count_ == 1);
    REQUIRE(b.call_count_ == 1);
    REQUIRE(c.call_count_ == 1);
}

TEST_CASE("MainPoller: repeated run_once calls all pollables each time", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable s1, s2;
    s1.reset(); s2.reset();

    poller.add(&s1);
    poller.add(&s2);

    for (int i = 0; i < 10; i++) {
        poller.run_once();
    }

    REQUIRE(s1.call_count_ == 10);
    REQUIRE(s2.call_count_ == 10);
}

TEST_CASE("MainPoller: pollables called in insertion order", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable a("a"), b("b"), c("c"), d("d");
    a.reset(); b.reset(); c.reset(); d.reset();

    poller.add(&a);
    poller.add(&b);
    poller.add(&c);
    poller.add(&d);

    poller.run_once();

    // Order should be: a(0), b(1), c(2), d(3)
    REQUIRE(a.last_order_ == 0);
    REQUIRE(b.last_order_ == 1);
    REQUIRE(c.last_order_ == 2);
    REQUIRE(d.last_order_ == 3);
}

TEST_CASE("MainPoller: up to 8 pollables works", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable spies[8];

    for (int i = 0; i < 8; i++) {
        spies[i].reset();
        poller.add(&spies[i]);
    }

    poller.run_once();
    poller.run_once();

    for (int i = 0; i < 8; i++) {
        REQUIRE(spies[i].call_count_ == 2);
    }
}

TEST_CASE("ControlLoop: unlimited add — vector-based", "[poller][app]") {
    ControlLoopInteractor poller;
    SpyPollable spies[20];

    for (int i = 0; i < 20; i++) {
        spies[i].reset();
        REQUIRE(poller.add(&spies[i]) == true);
    }

    poller.run_once();

    for (int i = 0; i < 20; i++) {
        REQUIRE(spies[i].call_count_ == 1);
    }
}
