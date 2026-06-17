#pragma once

#include <cstdint>

typedef uint32_t TickType_t;

inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return (TickType_t)ms; }
