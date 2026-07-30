#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "application/ports/driven/iota_downloader.h"

/// Обёртка над esp_https_ota: скачивает образ прошивки и записывает его в
/// неактивный OTA-слот, сообщает прогресс и ошибку.
///
/// НЕ вызывает esp_restart() — перезапуск выполняет вызывающий слой
/// (interactor) после сброса статистики. После успешного download() образ
/// переходит в состояние ESP_OTA_IMG_PENDING_VERIFY: вызывающий слой
/// отвечает за mark_valid / откат (rollback).
class EspOtaAdapter : public IOtaDownloader {
public:
    enum class State : uint8_t {
        IDLE = 0,        ///< ожидание, загрузка ещё не запускалась
        DOWNLOADING,     ///< скачивание и запись в неактивный слот
        DONE,            ///< образ записан, ждёт mark_valid вызывающим слоем
        FAILED,          ///< ошибка (см. last_error())
    };

    EspOtaAdapter();
    ~EspOtaAdapter();

    /// Скачать образ для заданного тега и записать в неактивный слот.
    /// URL = "https://sogimu.github.io/esp-ot-gateway/firmware/" + tag + "/esp-ot-gateway.bin".
    /// Возвращает true при успехе. При ошибке заполняет last_error().
    /// Внутри выполняет корректную последовательность
    /// esp_https_ota_begin → perform(цикл) → finish.
    bool download(const char* tag) override;

    State       state() const { return state_; }
    int         progress_pct() const override { return progress_pct_; }
    const char* last_error() const { return last_error_; }  ///< только из задачи загрузки
    /// Потокобезопасная копия last_error для межзадачного чтения (напр. из poll()):
    /// берёт внутренний мьютекс, исключая torn-read строки.
    void copy_last_error(char* dst, size_t n) const override;

private:
    /// Записать сообщение об ошибке (printf-стиль) в last_error_.
    void set_last_error(const char* fmt, ...);

    State state_         = State::IDLE;
    int   progress_pct_  = 0;
    char  last_error_[96] = {0};
    mutable SemaphoreHandle_t err_lock_ = nullptr;  ///< защита last_error_ от межзадачного torn-read
};
