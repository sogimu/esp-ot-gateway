#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include "application/ports/driving/iota_manager.h"  // IOtaManager, OtaStatus
#include "application/ports/driven/iota_validity.h"
#include "application/ports/driven/iota_downloader.h"
#include "application/ports/driven/iota_version_index.h"

/// Оркестратор OTA: реализует IOtaManager, координируя три driven-порта:
///   - IOtaValidity       — подтверждение/откат прошивки (is_pending, mark_invalid_and_reboot);
///   - IOtaDownloader     — скачивание и запись образа в неактивный слот;
///   - IOtaVersionIndex   — каталог версий (versions.json).
///
/// Класс свободен от аппаратных подробностей (FreeRTOS/ESP-IDF): для синхронизации
/// используется std::mutex, монотонное время инъецируется (now_ms). Запуск фонового
/// загрузчика (FreeRTOS-задача) и перезагрузка в новый слот — виртуальные швы
/// launch_download_task()/reboot_into_new_slot(), которые реализует конкретный
/// FirmwareOtaInteractor (прошивка) или TestOtaInteractor (тесты).
///
/// Повторный begin_update во время загрузки отклоняется: состояние должно быть
/// IDLE, DONE или ERROR. После успешной загрузки — flush статистики (хук) и
/// reboot_into_new_slot().
class OtaInteractor : public IOtaManager {
public:
    /// Источник монотонного времени в миллисекундах (для TTL кэша версий).
    /// На прошивке — esp_timer_get_time()/1000; в тестах — управляемый счётчик.
    using NowMsFn = std::function<int64_t()>;

    OtaInteractor(IOtaValidity& validity,
                  IOtaDownloader& downloader,
                  IOtaVersionIndex& version_index,
                  NowMsFn now_ms);
    ~OtaInteractor() override;

    /// Хук сброса статистики в NVS перед перезагрузкой в новый слот.
    using FlushStatsFn = void (*)(void* ctx);
    void set_flush_stats_callback(FlushStatsFn fn, void* ctx) {
        flush_fn_ = fn;
        flush_ctx_ = ctx;
    }

    // ── IOtaManager ────────────────────────────────────
    OtaStatus status() override;
    char*     fetch_version_list() override;  ///< heap JSON, caller frees; nullptr при ошибке
    bool      begin_update(const char* tag) override;
    void      rollback_now() override;
    void      poll() override;

protected:
    /// Шов: запустить загрузку образа. Вызывается из begin_update после перевода
    /// состояния в FETCHING. На прошивке — xTaskCreate; в тестах — запись признака
    /// (без реального запуска, run_download вызывается явно). true при успехе.
    virtual bool launch_download_task() = 0;

    /// Шов: перезагрузка в новый слот после успешной записи образа.
    /// На прошивке — vTaskDelay+esp_restart(); в тестах — запись признака (без ребута).
    virtual void reboot_into_new_slot() = 0;

    /// Тело загрузки: вызов downloader.download(), обновление состояния (DONE/ERROR),
    /// при успехе — flush статистики и reboot_into_new_slot(). Вызывается реализацией
    /// launch_download_task().
    void run_download();

private:
    /// Перенос прогресса/ошибки из downloader в status_ (под захваченным mutex_).
    void sync_progress_locked();

    IOtaValidity&        validity_;
    IOtaDownloader&      downloader_;
    IOtaVersionIndex&    version_index_;
    NowMsFn              now_ms_;

    std::mutex           mutex_;
    OtaStatus            status_{};

    // Кэш каталога версий (~5 минут)
    char*                cached_versions_ = nullptr;
    int64_t              last_fetch_ms_   = 0;
    static constexpr int64_t CACHE_TTL_MS = 5LL * 60LL * 1000LL;  // 5 минут

    // Хук flush stats
    FlushStatsFn         flush_fn_  = nullptr;
    void*                flush_ctx_ = nullptr;
};
