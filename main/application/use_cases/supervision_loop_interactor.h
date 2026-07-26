#pragma once
#include <cstdint>

class OtaValidityAdapter;
class EventLogAdapter;
class HttpControllerAdapter;
class WifiApStaAdapter;

/// Инкапсулирует задачи Supervision-цикла (~15с): OTA-валидность,
/// мониторинг кучи, AP-watchdog. Вызывается из главного цикла app_main.
class SupervisionLoopInteractor {
public:
    SupervisionLoopInteractor(OtaValidityAdapter& ota,
                               EventLogAdapter& log,
                               HttpControllerAdapter& http,
                               WifiApStaAdapter& wifi);

    /// Вызвать на каждой итерации главного цикла.
    void tick(uint32_t free_heap, uint32_t largest_free);

private:
    OtaValidityAdapter&   ota_;
    EventLogAdapter&      log_;
    HttpControllerAdapter& http_;
    WifiApStaAdapter&     wifi_;
};
