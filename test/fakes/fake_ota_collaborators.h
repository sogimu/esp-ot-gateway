#pragma once

#include "application/ports/driven/iota_downloader.h"
#include "application/ports/driven/iota_version_index.h"
#include "application/ports/driven/iota_validity.h"

#include <cstring>
#include <cstdlib>

/// Fake-загрузчик для хостовых тестов OtaInteractor.
/// Возвращает предустановленные значения, не зависит от ESP-IDF.
struct FakeOtaDownloader : IOtaDownloader {
    bool should_succeed = true;
    int  pct            = 0;
    char error[96]      = {0};

    bool download(const char*) override    { return should_succeed; }
    int  progress_pct() const override    { return pct; }
    void copy_last_error(char* dst, std::size_t n) const override {
        if (dst && n) {
            std::strncpy(dst, error, n - 1);
            dst[n - 1] = '\0';
        }
    }
};

/// Fake-индекс версий для хостовых тестов OtaInteractor.
struct FakeOtaVersionIndex : IOtaVersionIndex {
    char* next_response = nullptr;  ///< что вернёт следующий вызов (heap, передаётся владение)
    int   fetch_count   = 0;

    ~FakeOtaVersionIndex() { std::free(next_response); }

    char* fetch_versions() override {
        ++fetch_count;
        if (next_response == nullptr) return nullptr;
        // Отдаём копию, повторяя контракт реального адаптера (caller frees).
        size_t n = std::strlen(next_response);
        char* p = static_cast<char*>(std::malloc(n + 1));
        if (p) std::memcpy(p, next_response, n + 1);
        return p;
    }
};

/// Fake-валидатор для хостовых тестов OtaInteractor.
struct FakeOtaValidity : IOtaValidity {
    bool pending           = false;
    bool reboot_called     = false;

    bool is_pending() const override             { return pending; }
    void mark_invalid_and_reboot() override { reboot_called = true; }
};
