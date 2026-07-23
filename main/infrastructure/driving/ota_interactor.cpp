#include "infrastructure/driving/ota_interactor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"

static const char* TAG = "ota_mgr";

namespace {
char* dup_json(const char* s) {
    if (s == nullptr) return nullptr;
    size_t n = strlen(s);
    char* p = static_cast<char*>(malloc(n + 1));
    if (p != nullptr) memcpy(p, s, n + 1);
    return p;
}
}

OtaInteractor::OtaInteractor(IOtaValidity&     validity,
                             IOtaDownloader&   downloader,
                             IOtaVersionIndex& version_index,
                             NowMsFn            now_ms,
                             SpawnFn            spawn,
                             RebootFn           reboot)
    : validity_(validity), downloader_(downloader), version_index_(version_index),
      now_ms_(std::move(now_ms)), spawn_fn_(std::move(spawn)), reboot_fn_(std::move(reboot))
{
    memset(&status_, 0, sizeof(status_));
    status_.state          = OtaStatus::IDLE;
    status_.rollback_pending = validity_.is_pending();
    strncpy(status_.current_version, FIRMWARE_VERSION, sizeof(status_.current_version) - 1);
    status_.current_version[sizeof(status_.current_version) - 1] = '\0';
    if (status_.rollback_pending) status_.state = OtaStatus::VERIFY_PENDING;
    ESP_LOGI(TAG, "init: version=%s pending=%d", status_.current_version, status_.rollback_pending ? 1 : 0);
}

OtaInteractor::~OtaInteractor() { free(cached_versions_); }

OtaStatus OtaInteractor::status() {
    std::lock_guard<std::mutex> lk(mutex_);
    OtaStatus snap = status_;
    snap.rollback_pending = validity_.is_pending();
    return snap;
}

char* OtaInteractor::fetch_version_list() {
    const int64_t now_ms = now_ms_();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (cached_versions_ && (now_ms - last_fetch_ms_) < CACHE_TTL_MS)
            return dup_json(cached_versions_);
    }
    char* fresh = version_index_.fetch_versions();
    if (!fresh) { ESP_LOGW(TAG, "fetch_version_list: не удалось"); return nullptr; }
    std::lock_guard<std::mutex> lk(mutex_);
    free(cached_versions_); cached_versions_ = fresh; last_fetch_ms_ = now_ms;
    return dup_json(cached_versions_);
}

bool OtaInteractor::begin_update(const char* tag) {
    if (!tag || !tag[0]) return false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (status_.state != OtaStatus::IDLE &&
            status_.state != OtaStatus::DONE &&
            status_.state != OtaStatus::ERROR) {
            ESP_LOGW(TAG, "begin_update: занят (state=%d)", (int)status_.state);
            return false;
        }
        strncpy(status_.target_tag, tag, sizeof(status_.target_tag) - 1);
        status_.target_tag[sizeof(status_.target_tag) - 1] = '\0';
        status_.progress_pct  = 0;
        status_.last_error[0] = '\0';
        status_.state = OtaStatus::FETCHING;
    }
    ESP_LOGI(TAG, "begin_update(%s)", tag);
    if (!spawn_fn_(this)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.state = OtaStatus::ERROR;
        snprintf(status_.last_error, sizeof(status_.last_error), "не удалось запустить загрузку");
        return false;
    }
    return true;
}

void OtaInteractor::rollback_now() {
    ESP_LOGW(TAG, "rollback_now: ручной откат");
    validity_.mark_invalid_and_reboot();
}

void OtaInteractor::poll() {
    std::lock_guard<std::mutex> lk(mutex_);
    sync_progress_locked();
    status_.rollback_pending = validity_.is_pending();
}

void OtaInteractor::run_download() {
    char tag[32];
    {
        std::lock_guard<std::mutex> lk(mutex_);
        strncpy(tag, status_.target_tag, sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';
        status_.state = OtaStatus::WRITING;
        status_.progress_pct = 0;
    }
    ESP_LOGI(TAG, "download(%s)", tag);
    bool ok = downloader_.download(tag);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sync_progress_locked();
        if (ok) { status_.progress_pct = 100; status_.state = OtaStatus::DONE; }
        else {
            status_.state = OtaStatus::ERROR;
            if (status_.last_error[0] == '\0')
                snprintf(status_.last_error, sizeof(status_.last_error), "ошибка загрузки");
        }
    }
    if (ok) {
        if (flush_fn_) flush_fn_(flush_ctx_);
        ESP_LOGI(TAG, "перезагрузка в новый слот");
        reboot_fn_();
    }
}

void OtaInteractor::sync_progress_locked() {
    if (status_.state != OtaStatus::FETCHING && status_.state != OtaStatus::WRITING) return;
    int pct = downloader_.progress_pct();
    if (pct > 0) status_.progress_pct = pct;
    char err[96];
    downloader_.copy_last_error(err, sizeof(err));
    if (err[0]) {
        strncpy(status_.last_error, err, sizeof(status_.last_error) - 1);
        status_.last_error[sizeof(status_.last_error) - 1] = '\0';
    }
}
