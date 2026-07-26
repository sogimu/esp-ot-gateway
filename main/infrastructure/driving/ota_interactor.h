#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include "application/ports/driving/iota_manager.h"
#include "application/ports/driving/icontrol_task.h"
#include "application/ports/driven/iota_validity.h"
#include "application/ports/driven/iota_downloader.h"
#include "application/ports/driven/iota_version_index.h"

/// Оркестратор OTA: реализует IOtaManager и IControlTask. Свободен от железа —
/// FreeRTOS/esp_restart инжектятся через std::function в конструкторе,
/// что позволяет тестировать логику на хосте с заглушками.
class OtaInteractor : public IOtaManager, public IControlTask {
public:
    using NowMsFn = std::function<int64_t()>;

    /// Запустить фоновую загрузку (вызывается из begin_update).
    /// Должен вызвать self->run_download() — на прошивке в FreeRTOS-задаче,
    /// в тестах синхронно. true при успешном запуске.
    using SpawnFn = std::function<bool(OtaInteractor* self)>;

    /// Перезагрузка в новый слот после успешной загрузки.
    using RebootFn = std::function<void()>;

    OtaInteractor(IOtaValidity& validity,
                  IOtaDownloader& downloader,
                  IOtaVersionIndex& version_index,
                  NowMsFn now_ms,
                  SpawnFn spawn,
                  RebootFn reboot);
    ~OtaInteractor() override;

    using FlushStatsFn = void (*)(void* ctx);
    void set_flush_stats_callback(FlushStatsFn fn, void* ctx) {
        flush_fn_ = fn; flush_ctx_ = ctx;
    }

    // ── IOtaManager ────────────────────────────────────
    OtaStatus status() override;
    char*     fetch_version_list() override;
    bool      begin_update(const char* tag) override;
    void      rollback_now() override;
    void      poll() override;     // IOtaManager — синхронизация прогресса

    // ── IControlTask ─────────────────────────────────────
    void execute() override { poll(); }

    /// Тело загрузки (public — вызывается из spawn-лямбды).
    void run_download();

private:
    void sync_progress_locked();

    IOtaValidity&        validity_;
    IOtaDownloader&      downloader_;
    IOtaVersionIndex&    version_index_;
    NowMsFn              now_ms_;
    SpawnFn              spawn_fn_;
    RebootFn             reboot_fn_;

    std::mutex           mutex_;
    OtaStatus            status_{};

    char*                cached_versions_ = nullptr;
    int64_t              last_fetch_ms_   = 0;
    static constexpr int64_t CACHE_TTL_MS = 5LL * 60LL * 1000LL;

    FlushStatsFn         flush_fn_  = nullptr;
    void*                flush_ctx_ = nullptr;
};
