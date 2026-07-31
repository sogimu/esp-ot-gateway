#pragma once

#include "application/ports/driven/iota_version_index.h"
#include "application/ports/driven/iota_validity.h"

#include <cstring>
#include <cstdlib>

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
