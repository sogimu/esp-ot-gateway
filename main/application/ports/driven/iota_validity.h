#pragma once

/// Driven-порт: подтверждение валидности свежезалитой прошивки и откат.
///
/// Конкретная реализация (OtaValidityAdapter) использует esp_ota_* (состояние
/// партиции, mark_valid/mark_invalid) и таймер. OtaInteractor видит только этот
/// интерфейс → тестируется с fake.
///
/// Методы arm()/heartbeat()/set_crash_flag() для здоровья при загрузке вызывает
/// напрямую main.cpp (по конкретному адаптеру) — они не входят в этот порт,
/// т.к. относятся к стартовой валидации, а не к управлению OTA со стороны UI.
class IOtaValidity {
public:
    virtual ~IOtaValidity() = default;

    /// Загруженный образ ожидает подтверждения (ESP_OTA_IMG_PENDING_VERIFY).
    virtual bool is_pending() const = 0;

    /// Немедленный ручной откат: пометить текущий образ invalid и перезагрузиться
    /// в предыдущий слот. На реальном устройстве не возвращает управление.
    virtual void mark_invalid_and_reboot() = 0;
};
