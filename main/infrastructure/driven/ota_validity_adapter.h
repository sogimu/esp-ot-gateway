#pragma once

#include <atomic>
#include <cstdint>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"       // esp_timer_get_time() — монотонное время со старта
#include "esp_ota_ops.h"

#include "application/ports/driven/iota_validity.h"

/// Подтверждение валидности свежезалитой прошивки и авто-откат.
///
/// В A/B-схеме новая прошивка загружается в состоянии
/// ESP_OTA_IMG_PENDING_VERIFY и должна за 90 с доказать, что она «живая».
/// Критерий здоровья: HTTP-сервер поднят + главный цикл тикает (сам факт
/// вызова heartbeat()). На первом здоровом тике после arm() вызывается
/// esp_ota_mark_app_valid_cancel_rollback(). Если за 90 с здоровье не
/// подтверждено — esp_ota_mark_app_invalid_rollback_and_reboot().
///
/// КРАШ-ПУТЬ: если загрузка в pending и предыдущий старт был крашем,
/// arm() немедленно откатывает прошивку, не дожидаясь таймера.
///
/// Связность: чтобы не тянуть зависимость на CrashDiagnosticsAdapter,
/// признак краша предыдущей загрузки передаётся через set_crash_flag() из
/// main.cpp (после создания crash_diag). Признак поднятого HTTP-сервера —
/// через set_http_server_up().
class OtaValidityAdapter : public IOtaValidity {
public:
    OtaValidityAdapter();
    ~OtaValidityAdapter();

    /// Загруженный образ находится в состоянии PENDING_VERIFY
    /// (требуется подтверждение валидности в течение окна).
    bool is_pending() const override { return pending_; }

    /// IOtaValidity: немедленный ручной откат (помечает текущий образ invalid
    /// и перезагружается в предыдущий слот). На устройстве не возвращает управление.
    void mark_invalid_and_reboot() override;

    /// Взвести дедлайн валидации. Запускать после поднятия HTTP-сервера.
    /// Идемпотентен. При pending_ + краше предыдущей загрузки выполняет
    /// немедленный откат. Вне состояния pending — no-op.
    void arm();

    /// Тик главного цикла (~каждые 15 с). На первом здоровом тике после
    /// arm() помечает прошивку валидной; при истечении дедлайна — откат.
    void heartbeat();

    /// Признак того, что HTTP-сервер поднят (для расчёта health).
    void set_http_server_up(bool up) { http_server_up_ = up; }

    /// Признак краша предыдущей загрузки (источник — CrashDiagnosticsAdapter,
    /// передаётся из main.cpp, чтобы не связывать слои напрямую).
    void set_crash_flag(bool crash) { prev_boot_crash_ = crash; }

    /// Глобальный доступ к признаку pending для других слоёв (NVS-заморозка
    /// в D9, D10). Возвращает false, если экземпляр ещё не создан.
    static bool is_pending_global();

    /// Глобальный флаг: идёт OTA-flush (сохранение статистики перед
    /// перезагрузкой в новый слот). PersistenceLoopInteractor проверяет
    /// и пропускает периодический save, чтобы избежать race.
    static bool is_flushing_global() { return flushing_.load(); }
    static void set_flushing_global(bool v) { flushing_.store(v); }

private:
    /// Помечает прошивку валидной (mark_app_valid_cancel_rollback),
    /// логирует успех. Идемпотентна.
    void mark_valid();

    /// Немедленный откат: mark_app_invalid_rollback_and_reboot() с логом
    /// причины. Не возвращает управление (перезагрузка).
    void rollback_and_reboot(const char* reason);

    const esp_partition_t* running_ = nullptr;  ///< текущая (загруженная) партиция
    bool pending_         = false;   ///< образ в состоянии PENDING_VERIFY
    bool armed_           = false;   ///< arm() вызван, дедлайн взведён
    bool http_server_up_  = false;   ///< HTTP-сервер поднят
    bool prev_boot_crash_ = false;   ///< предыдущая загрузка — краш
    bool marked_valid_    = false;   ///< mark_app_valid уже выполнен

    int64_t deadline_us_ = 0;        ///< arm_time + VALIDITY_WINDOW_US (monotonic)

    static OtaValidityAdapter* instance_;  ///< единственный экземпляр для is_pending_global()
    static std::atomic<bool>   flushing_;  ///< true во время ota_flush_and_reboot

    /// Окно подтверждения валидности: 90 с (в микросекундах).
    static constexpr int64_t VALIDITY_WINDOW_US = 90LL * 1000LL * 1000LL;
};
