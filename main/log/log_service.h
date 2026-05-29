#pragma once

#include "model/model.h"

#include <cstdarg>

class LogService {
public:
    explicit LogService(Model& model);

    void start();
    void stop();

    void event(LogCategory cat, const char* fmt, ...);

private:
    Model& model_;
    bool started_ = false;
};