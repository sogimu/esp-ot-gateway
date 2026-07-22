#include "infrastructure/driving/ota_interactor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"   // тонкий лог-стаб в хостовых тестах, реальный ESP_LOG в прошивке

[[maybe_unused]] static const char* TAG = "ota_mgr";

// ───────────────────────────────────────────────────────────────────────────
// Вспомогательное: копирование NUL-terminated строки в кучу (caller frees).
// ───────────────────────────────────────────────────────────────────────────
namespace {
char* dup_json(const char* s)
{
    if (s == nullptr) {
        return nullptr;
    }
    size_t n = strlen(s);
    char* p = static_cast<char*>(malloc(n + 1));
    if (p != nullptr) {
        memcpy(p, s, n + 1);
    }
    return p;
}
}  // namespace

// ───────────────────────────────────────────────────────────────────────────
// Конструктор / деструктор
// ───────────────────────────────────────────────────────────────────────────
OtaInteractor::OtaInteractor(IOtaValidity&     validity,
                             IOtaDownloader&   downloader,
                             IOtaVersionIndex& version_index,
                             NowMsFn            now_ms)
    : validity_(validity), downloader_(downloader), version_index_(version_index),
      now_ms_(std::move(now_ms))
{
    memset(&status_, 0, sizeof(status_));
    status_.state          = OtaStatus::IDLE;
    status_.progress_pct   = 0;
    status_.last_error[0]  = '\0';
    status_.target_tag[0]  = '\0';
    status_.rollback_pending = validity_.is_pending();

    strncpy(status_.current_version, FIRMWARE_VERSION,
            sizeof(status_.current_version) - 1);
    status_.current_version[sizeof(status_.current_version) - 1] = '\0';

    if (status_.rollback_pending) {
        status_.state = OtaStatus::VERIFY_PENDING;
    }

    ESP_LOGI(TAG, "init: version=%s pending=%d state=%d",
             status_.current_version, status_.rollback_pending ? 1 : 0,
             (int)status_.state);
}

OtaInteractor::~OtaInteractor()
{
    free(cached_versions_);
    cached_versions_ = nullptr;
}

// ───────────────────────────────────────────────────────────────────────────
// status() — снимок текущего состояния (потокобезопасно).
// ───────────────────────────────────────────────────────────────────────────
OtaStatus OtaInteractor::status()
{
    std::lock_guard<std::mutex> lk(mutex_);
    OtaStatus snap = status_;
    snap.rollback_pending = validity_.is_pending();
    return snap;
}

// ───────────────────────────────────────────────────────────────────────────
// fetch_version_list() — каталог версий через IOtaVersionIndex с кэшем ~5 мин.
// ───────────────────────────────────────────────────────────────────────────
char* OtaInteractor::fetch_version_list()
{
    const int64_t now_ms = now_ms_();

    // 1) Есть ли валидный кэш? Отдаём копию.
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool cache_valid = (cached_versions_ != nullptr) &&
                                 ((now_ms - last_fetch_ms_) < CACHE_TTL_MS);
        if (cache_valid) {
            return dup_json(cached_versions_);
        }
    }

    // 2) Кэш пуст/просрочен — загружаем.
    char* fresh = version_index_.fetch_versions();  // heap JSON либо nullptr
    if (fresh == nullptr) {
        ESP_LOGW(TAG, "fetch_version_list: не удалось скачать индекс версий");
        return nullptr;
    }

    // 3) Сохраняем в кэш и отдаём копию.
    std::lock_guard<std::mutex> lk(mutex_);
    free(cached_versions_);
    cached_versions_ = fresh;          // забираем владение
    last_fetch_ms_   = now_ms;
    return dup_json(cached_versions_);
}

// ───────────────────────────────────────────────────────────────────────────
// begin_update(tag) — запуск обновления. Отклоняется, если загрузка уже идёт.
// ───────────────────────────────────────────────────────────────────────────
bool OtaInteractor::begin_update(const char* tag)
{
    if (tag == nullptr || tag[0] == '\0') {
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (status_.state != OtaStatus::IDLE &&
            status_.state != OtaStatus::DONE &&
            status_.state != OtaStatus::ERROR) {
            ESP_LOGW(TAG, "begin_update(%s): отклонено — обновление уже идёт (state=%d)",
                     tag, (int)status_.state);
            return false;
        }

        strncpy(status_.target_tag, tag, sizeof(status_.target_tag) - 1);
        status_.target_tag[sizeof(status_.target_tag) - 1] = '\0';
        status_.progress_pct  = 0;
        status_.last_error[0] = '\0';
        status_.state         = OtaStatus::FETCHING;
    }

    ESP_LOGI(TAG, "begin_update(%s): запуск загрузки", tag);

    if (!launch_download_task()) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.state = OtaStatus::ERROR;
        snprintf(status_.last_error, sizeof(status_.last_error),
                 "не удалось запустить загрузку");
        return false;
    }
    return true;
}

// ───────────────────────────────────────────────────────────────────────────
// rollback_now() — немедленный ручной откат (через IOtaValidity).
// ───────────────────────────────────────────────────────────────────────────
void OtaInteractor::rollback_now()
{
    ESP_LOGW(TAG, "rollback_now: ручной откат прошивки");
    validity_.mark_invalid_and_reboot();
    // На устройстве mark_invalid_and_reboot() перезагружает и не возвращает.
}

// ───────────────────────────────────────────────────────────────────────────
// poll() — вызывается из главного цикла: обновить прогресс + ре-чек валидности.
// ───────────────────────────────────────────────────────────────────────────
void OtaInteractor::poll()
{
    std::lock_guard<std::mutex> lk(mutex_);
    sync_progress_locked();
    status_.rollback_pending = validity_.is_pending();
}

// ───────────────────────────────────────────────────────────────────────────
// run_download() — тело загрузки образа (вызывается из launch-шва).
// ───────────────────────────────────────────────────────────────────────────
void OtaInteractor::run_download()
{
    char tag[32];
    {
        std::lock_guard<std::mutex> lk(mutex_);
        strncpy(tag, status_.target_tag, sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';
        status_.state        = OtaStatus::WRITING;
        status_.progress_pct = 0;
    }

    ESP_LOGI(TAG, "OTA-задача: download(%s)", tag);
    const bool ok = downloader_.download(tag);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        sync_progress_locked();
        if (ok) {
            status_.progress_pct = 100;
            status_.state        = OtaStatus::DONE;
            ESP_LOGI(TAG, "OTA: образ залит, state=DONE");
        } else {
            status_.state = OtaStatus::ERROR;
            if (status_.last_error[0] == '\0') {
                snprintf(status_.last_error, sizeof(status_.last_error),
                         "ошибка загрузки образа");
            }
            ESP_LOGE(TAG, "OTA: сбой download, state=ERROR (%s)", status_.last_error);
        }
    }

    if (ok) {
        // flush статистики — вызывается из run_download до ребута, поэтому
        // flush_fn_ не нужно выносить в шов (run_download — метод этого же класса).
        if (flush_fn_ != nullptr) {
            flush_fn_(flush_ctx_);
        }
        ESP_LOGI(TAG, "OTA: перезагрузка в новый слот");
        reboot_into_new_slot();
        // На устройстве не возвращаемся.
    }
}

// ───────────────────────────────────────────────────────────────────────────
// sync_progress_locked() — перенос прогресса/ошибки из downloader в status_.
// ───────────────────────────────────────────────────────────────────────────
void OtaInteractor::sync_progress_locked()
{
    if (status_.state != OtaStatus::FETCHING &&
        status_.state != OtaStatus::WRITING) {
        return;
    }

    const int pct = downloader_.progress_pct();
    if (pct > 0) {
        status_.progress_pct = pct;
    }

    char err[96];
    downloader_.copy_last_error(err, sizeof(err));
    if (err[0] != '\0') {
        strncpy(status_.last_error, err, sizeof(status_.last_error) - 1);
        status_.last_error[sizeof(status_.last_error) - 1] = '\0';
    }
}
