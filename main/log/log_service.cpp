#include "log/log_service.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdarg>

LogService::LogService(Model& model)
    : model_(model)
{
}

void LogService::start()
{
    started_ = true;
}

void LogService::stop()
{
    started_ = false;
}

void LogService::event(LogCategory cat, const char* fmt, ...)
{
    time_t now;
    time(&now);
    uint32_t ts = (uint32_t)(now > 0 ? now : 0);

    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    model_.add_log_entry(ts, (uint8_t)cat, buf);
}