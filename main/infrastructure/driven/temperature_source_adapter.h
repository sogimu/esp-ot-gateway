#pragma once

#include "application/ports/driven/itemperatures_source.h"

class IHeatingStateStore;

/// ITemperatureSource adapter that decorates IHeatingStateStore and applies
/// per-temperature user offsets (corrected = raw + offset).
class TemperatureSourceAdapter : public ITemperatureSource {
public:
    explicit TemperatureSourceAdapter(IHeatingStateStore& store) : store_(store) {}

    float t1() override;
    float t2() override;
    float outside() override;
    float ch_flow() override;
    float ch_return() override;
    float dhw() override;

private:
    IHeatingStateStore& store_;
};
