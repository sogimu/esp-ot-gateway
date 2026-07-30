#pragma once

/// Each pollable entity is called once per cycle (~1.1 s).
/// Implemented by BoilerPollInteractor, SensorsPollInteractor, PidPollInteractor.
class IControlTask {
public:
    virtual void execute() = 0;
    virtual ~IControlTask() = default;
};
