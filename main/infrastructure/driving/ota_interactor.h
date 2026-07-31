#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include "application/ports/driving/iota_manager.h"
#include "application/ports/driving/icontrol_task.h"
#include "application/ports/driven/iota_validity.h"
#include "application/ports/driven/iota_version_index.h"

/// Оркестратор OTA: версии, откат, SHA256. Загрузка — через upload.
class OtaInteractor : public IOtaManager, public IControlTask {
public:
    using NowMsFn = std::function<int64_t()>;

    OtaInteractor(IOtaValidity& validity,
                  IOtaVersionIndex& version_index,
                  NowMsFn now_ms);
    ~OtaInteractor() override;

    // ── IOtaManager ────────────────────────────────────
    OtaStatus status() override;
    char*     fetch_version_list() override;
    const char* lookup_sha256(const char* tag) override;
    bool      begin_update(const char* tag) override { (void)tag; return false; }
    void      rollback_now() override;
    void      poll() override;

    // ── IControlTask ─────────────────────────────────────
    void execute() override { poll(); }

private:
    IOtaValidity&        validity_;
    IOtaVersionIndex&    version_index_;
    NowMsFn              now_ms_;

    std::mutex           mutex_;
    OtaStatus            status_{};

    char*                cached_versions_ = nullptr;
    int64_t              last_fetch_ms_   = 0;
    static constexpr int64_t CACHE_TTL_MS = 60LL * 1000LL;
};
