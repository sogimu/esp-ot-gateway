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
                             IOtaVersionIndex& version_index,
                             NowMsFn            now_ms)
    : validity_(validity), version_index_(version_index),
      now_ms_(std::move(now_ms))
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

const char* OtaInteractor::lookup_sha256(const char* tag)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!cached_versions_ || !tag) return nullptr;

    char tag_key[48];
    snprintf(tag_key, sizeof(tag_key), "\"tag\":\"%s\"", tag);
    const char* pos = strstr(cached_versions_, tag_key);
    if (!pos) return nullptr;

    const char* sha = strstr(pos, "\"sha256\":\"");
    if (!sha) return nullptr;
    sha += 10;
    const char* end = strchr(sha, '"');
    size_t len = end ? (size_t)(end - sha) : 64;
    if (len > 64) len = 64;
    static char buf[65];
    memcpy(buf, sha, len);
    buf[len] = '\0';
    return buf;
}

void OtaInteractor::rollback_now() {
    ESP_LOGW(TAG, "rollback_now: ручной откат");
    validity_.mark_invalid_and_reboot();
}

void OtaInteractor::poll() {
    std::lock_guard<std::mutex> lk(mutex_);
    status_.rollback_pending = validity_.is_pending();
    if (!status_.rollback_pending && status_.state == OtaStatus::VERIFY_PENDING) {
        status_.state = OtaStatus::IDLE;
    }
}
