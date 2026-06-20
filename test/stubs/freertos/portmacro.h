#pragma once

// Minimal stub for ESP-IDF FreeRTOS portmacro.h
// Provides types needed by event_log_adapter.h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// portMUX_TYPE for spinlocks (used by EventLogAdapter)
typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

// Basic FreeRTOS types
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;

// Tick type
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX

// Stack type
typedef uint32_t StackType_t;

// Inline critical section macros (no-op in tests)
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux)  ((void)(mux))

#ifdef __cplusplus
}
#endif
