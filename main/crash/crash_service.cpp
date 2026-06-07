#include "crash/crash_service.h"

#include "esp_log.h"

#include <cstdio>

static const char *TAG = "crash";

CrashService::CrashService(LogService& log)
    : log_(log)
{
}

void CrashService::start()
{
    started_ = true;
    check_on_boot();
}

void CrashService::stop()
{
    started_ = false;
}

const char* CrashService::reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "подача питания";
        case ESP_RST_EXT:       return "внешний сброс";
        case ESP_RST_SW:        return "программный (esp_restart)";
        case ESP_RST_PANIC:     return "паника (exception)";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "другой watchdog";
        case ESP_RST_DEEPSLEEP: return "выход из deep sleep";
        case ESP_RST_BROWNOUT:  return "просадка питания";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        case ESP_RST_EFUSE:     return "efuse error";
        case ESP_RST_PWR_GLITCH:return "помеха питания";
        case ESP_RST_CPU_LOCKUP:return "блокировка CPU";
        default:                return "неизвестна";
    }
}

void CrashService::check_on_boot()
{
    // 1. Причина перезагрузки из RTC-регистра (переживает reset)
    esp_reset_reason_t reason = esp_reset_reason();
    log_.event(LOG_CAT_BOOT, "Запуск: %s", reset_reason_str(reason));

    // 2. Проверка core dump с предыдущего падения
    esp_err_t cd_err = esp_core_dump_image_check();
    if (cd_err == ESP_OK) {
        esp_core_dump_summary_t summary;
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Crash: %s PC=0x%08lx exccause=%lu",
                     summary.exc_task, summary.exc_pc,
                     (unsigned long)summary.ex_info.exc_cause);
            log_.event(LOG_CAT_BOOT, buf);

            // Бекстрейс: адреса одной строкой для копирования
            int bt_len = snprintf(buf, sizeof(buf), "bt:");
            for (int i = 0; i < 3 && i < (int)summary.exc_bt_info.depth; i++) {
                bt_len += snprintf(buf + bt_len, sizeof(buf) - bt_len,
                                   " 0x%08lx", summary.exc_bt_info.bt[i]);
            }
            log_.event(LOG_CAT_BOOT, buf);
            log_.event(LOG_CAT_BOOT, "Расшифровка: ./decode_crash.sh <адреса>");
        }
        esp_core_dump_image_erase();
    } else if (cd_err == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "No coredump found");
    } else {
        log_.event(LOG_CAT_BOOT, "Coredump повреждён (CRC)");
        esp_core_dump_image_erase();
        ESP_LOGW(TAG, "Coredump corrupted, erased");
    }
}
