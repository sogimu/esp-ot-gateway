/// Tests for Kalman1D filter (domain/services/kalman1d.h)
/// Covers: initialization, convergence, noise filtering, reset.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "domain/services/kalman1d.h"
#include <cmath>

using Catch::Approx;

TEST_CASE("Kalman1D: construction and initial value", "[kalman1d][domain]") {
    Kalman1D k(25.0f, 0.1f, 1.0f);
    // No getter for x_ — test via update
    float result = k.update(25.0f);
    REQUIRE(result == Approx(25.0f).margin(0.5f));
}

TEST_CASE("Kalman1D: converges to constant measurement", "[kalman1d][domain]") {
    Kalman1D k(0.0f, 0.1f, 1.0f);

    for (int i = 0; i < 50; i++) {
        k.update(50.0f);
    }

    float result = k.update(50.0f);
    REQUIRE(result == Approx(50.0f).margin(0.5f));
}

TEST_CASE("Kalman1D: filters noise", "[kalman1d][domain]") {
    Kalman1D k(40.0f, 0.01f, 5.0f); // low Q, high R → strong filtering

    float max_dev = 0;
    for (int i = 0; i < 100; i++) {
        // True value 40.0, noisy measurements ±2
        float noisy = 40.0f + 2.0f * sinf(static_cast<float>(i) * 0.7f);
        float filtered = k.update(noisy);
        float dev = std::abs(filtered - 40.0f);
        if (dev > max_dev) max_dev = dev;
    }

    // Filtered output should have less deviation than raw noise (±2.0)
    REQUIRE(max_dev < 2.0f);
}

TEST_CASE("Kalman1D: tracks step change", "[kalman1d][domain]") {
    Kalman1D k(20.0f, 0.5f, 1.0f); // high Q → responsive

    // Converge at 20
    for (int i = 0; i < 20; i++) k.update(20.0f);

    // Step to 60
    float last = 20.0f;
    bool moved_up = false;
    for (int i = 0; i < 20; i++) {
        last = k.update(60.0f);
        if (last > 30.0f) moved_up = true;
    }

    REQUIRE(moved_up);
    REQUIRE(last == Approx(60.0f).margin(2.0f));
}

TEST_CASE("Kalman1D: reset resets internal state", "[kalman1d][domain]") {
    Kalman1D k(10.0f, 0.1f, 1.0f);

    for (int i = 0; i < 30; i++) k.update(80.0f);

    k.reset(10.0f);

    // First update after reset should be close to new init
    float result = k.update(10.0f);
    REQUIRE(result == Approx(10.0f).margin(0.5f));
}

TEST_CASE("Kalman1D: handles extreme values", "[kalman1d][domain]") {
    Kalman1D k(0.0f, 1.0f, 10.0f);

    float r = k.update(1000.0f);
    REQUIRE(std::isfinite(r));

    r = k.update(-100.0f);
    REQUIRE(std::isfinite(r));

    r = k.update(0.0f);
    REQUIRE(std::isfinite(r));
}
