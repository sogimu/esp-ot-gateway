#pragma once

/// Each pollable entity is called once per cycle (~1.1 s).
/// Implemented by BoilerPollInteractor, SensorsPollInteractor, PidPollInteractor.
class IPollable {
public:
    virtual void poll() = 0;
    virtual ~IPollable() = default;
};
