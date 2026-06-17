#pragma once

#include "freertos/FreeRTOS.h"

/// Stub: overridden by do_delay_ms() in TestableWifiAdapter.
/// Real implementation is never called during tests, but linker needs a symbol.
inline void vTaskDelay(TickType_t) { /* never called in tests */ }

/// Additional FreeRTOS types needed by DNS server stubs.
typedef void* TaskHandle_t;

/// Stub: DNS server uses xTaskCreate — no-op on host.
inline void xTaskCreate(void (*)(void*), const char*, uint32_t,
                        void*, uint32_t, TaskHandle_t*) {}
inline void vTaskDelete(TaskHandle_t) {}
