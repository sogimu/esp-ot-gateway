#pragma once

/// Stub: overridden by do_reboot() in TestableWifiAdapter.
/// Real implementation is never called during tests, but linker needs a symbol.
inline void esp_restart() { /* never called in tests — do_reboot() is overridden */ }
