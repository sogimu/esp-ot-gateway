#pragma once
#include <cstdint>

class OtaValidityAdapter;
class EventLogAdapter;
class HttpControllerAdapter;
class WifiApStaAdapter;
class ITimeSource;

/// Инкапсулирует задачи Supervision-цикла (~15с): OTA-валидность,
/// мониторинг кучи, AP-watchdog, логирование CPU/heap/stats.
class SupervisionLoopInteractor {
public:
    SupervisionLoopInteractor(OtaValidityAdapter& ota,
                               EventLogAdapter& log,
                               HttpControllerAdapter& http,
                               WifiApStaAdapter& wifi,
                               ITimeSource& time);

    /// Вызвать на каждой итерации главного цикла.
    void tick();

private:
    OtaValidityAdapter&   ota_;
    EventLogAdapter&      log_;
    HttpControllerAdapter& http_;
    WifiApStaAdapter&     wifi_;
    ITimeSource&          time_;
    int                   cycle_ = 0;
};
