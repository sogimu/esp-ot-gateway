#pragma once

#include <cstddef>
#include <cstdint>

/// Driven-порт: низкоуровневая загрузка и запись OTA-образа.
///
/// Конкретная реализация (EspOtaAdapter) — обёртка над esp_https_ota и зависит
/// от ESP-IDF (HTTP/TLS/flash). OtaInteractor работает с загрузкой ТОЛЬКО через
/// этот интерфейс, поэтому его логику можно тестировать на хосте с fake-объектом,
/// без стабов аппаратных подробностей.
class IOtaDownloader {
public:
    virtual ~IOtaDownloader() = default;

    /// Скачать образ для заданного тега и записать в неактивный OTA-слот.
    /// Блокирует до завершения. true при успехе; при ошибке описание доступно
    /// через copy_last_error().
    virtual bool download(const char* tag) = 0;

    /// Текущий прогресс 0..100 (0, если размер неизвестен).
    virtual int progress_pct() const = 0;

    /// Потокобезопасная копия последней ошибки в dst (NUL-terminated).
    /// Безопасна для межзадачного чтения (берёт внутренний замок реализации).
    virtual void copy_last_error(char* dst, std::size_t n) const = 0;
};
