#pragma once

#include "log/log_service.h"

#include "esp_system.h"
#include "esp_core_dump.h"

class CrashService {
public:
    explicit CrashService(LogService& log);

    /* Проверить причину перезагрузки + coredump и записать в журнал.
     * Вызывается один раз при старте, до синхронизации времени (SNTP). */
    void start();
    void stop();

private:
    void check_on_boot();
    const char* reset_reason_str(esp_reset_reason_t reason);

    LogService& log_;
    bool started_ = false;
};
