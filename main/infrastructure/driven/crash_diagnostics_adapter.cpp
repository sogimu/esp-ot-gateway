#include "infrastructure/driven/crash_diagnostics_adapter.h"

#include "esp_log.h"

#include <cstdio>

static const char *TAG = "crash";

CrashDiagnosticsAdapter::CrashDiagnosticsAdapter(ILogger& log)
    : log_(log)
{
}

void CrashDiagnosticsAdapter::start()
{
    started_ = true;
    check_on_boot(log_);
}

void CrashDiagnosticsAdapter::stop()
{
    started_ = false;
}

const char* CrashDiagnosticsAdapter::reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software reset";
        case ESP_RST_PANIC:     return "panic/exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        case ESP_RST_EFUSE:     return "efuse error";
        case ESP_RST_PWR_GLITCH:return "power glitch";
        case ESP_RST_CPU_LOCKUP:return "CPU lockup";
        default:                return "unknown";
    }
}

void CrashDiagnosticsAdapter::check_on_boot(ILogger& log)
{
    esp_reset_reason_t reason = esp_reset_reason();
    log.event(ILogger::BOOT, "Boot: %s", reset_reason_str(reason));

    esp_err_t cd_err = esp_core_dump_image_check();
    if (cd_err == ESP_OK) {
        esp_core_dump_summary_t summary;
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Crash: %s PC=0x%08lx exccause=%lu",
                     summary.exc_task, summary.exc_pc,
                     (unsigned long)summary.ex_info.exc_cause);
            log.event(ILogger::BOOT, buf);

            int bt_len = snprintf(buf, sizeof(buf), "bt:");
            for (int i = 0; i < 3 && i < (int)summary.exc_bt_info.depth; i++) {
                bt_len += snprintf(buf + bt_len, sizeof(buf) - bt_len,
                                   " 0x%08lx", summary.exc_bt_info.bt[i]);
            }
            log.event(ILogger::BOOT, buf);
            log.event(ILogger::BOOT, "Decode: ./decode_crash.sh <addrs>");
        }
        esp_core_dump_image_erase();
    } else if (cd_err == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "No coredump found");
    } else {
        log.event(ILogger::BOOT, "Coredump corrupted (CRC)");
        esp_core_dump_image_erase();
        ESP_LOGW(TAG, "Coredump corrupted, erased");
    }
}
