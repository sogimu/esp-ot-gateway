/// Tests for signed f8.8 temperature decoding (OpenTherm).
/// Covers: negative/positive/fractional decoding, and that the raw
/// unsigned `read()` path is preserved for byte-pair/counter IDs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "infrastructure/driven/boiler_opentherm_adapter.h"
#include <iomanip>
#include <cmath>

using Catch::Approx;

namespace {

/// Fake IOtHardware — returns a pre-programmed OT_Frame response.
class FakeOtHardware : public IOtHardware {
public:
    bool init() override { return true; }

    bool send_and_receive(const void* /*request*/, void* response) override {
        if (!success_) return false;
        OT_Frame* resp = static_cast<OT_Frame*>(response);
        *resp = next_response_;
        return true;
    }

    OT_Frame next_response_{};
    bool     success_ = true;
};

} // namespace

TEST_CASE("read_temperature decodes signed f8.8 values", "[f88][temperature]") {
    struct Case { uint16_t raw; float expected; };
    const Case cases[] = {
        {0xFE00, -2.0f},   // -2.0°C
        {0x0000,  0.0f},   //  0.0°C
        {0xFF80, -0.5f},   // -0.5°C
        {0xCE00, -50.0f},  // -50.0°C
        {0x1400, 20.0f},   // +20.0°C
    };

    for (const auto& c : cases) {
        DYNAMIC_SECTION("raw 0x" << std::hex << c.raw) {
            FakeOtHardware hw;
            hw.next_response_.data_value = c.raw;
            BoilerOpenThermAdapter adapter(hw);

            auto r = adapter.read_temperature(27);

            REQUIRE(r.success);
            CHECK(r.value_f88 == Approx(c.expected));
        }
    }
}

TEST_CASE("read_temperature returns NAN on failed transaction", "[f88][temperature]") {
    FakeOtHardware hw;
    hw.success_ = false;
    BoilerOpenThermAdapter adapter(hw);

    auto r = adapter.read_temperature(27);

    REQUIRE_FALSE(r.success);
    CHECK(std::isnan(r.value_f88));
}

TEST_CASE("read() keeps unsigned interpretation (byte-pair/counter path)", "[f88][temperature]") {
    // The raw `read()` path is used for modulation, bounds and counters.
    // It MUST stay unsigned — negative temps must only go through read_temperature().
    FakeOtHardware hw;
    hw.next_response_.data_value = 0xFE00;
    BoilerOpenThermAdapter adapter(hw);

    auto r = adapter.read(27);

    REQUIRE(r.success);
    CHECK(r.value_f88 == Approx(254.0f));  // NOT -2.0
}

TEST_CASE("signed and unsigned paths diverge for the same raw value", "[f88][temperature]") {
    FakeOtHardware hw;
    hw.next_response_.data_value = 0xFE00;
    BoilerOpenThermAdapter adapter(hw);

    CHECK(adapter.read_temperature(27).value_f88 == Approx(-2.0f));
    CHECK(adapter.read(27).value_f88 == Approx(254.0f));
}
