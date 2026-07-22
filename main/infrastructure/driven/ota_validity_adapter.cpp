#include "infrastructure/driven/ota_validity_adapter.h"

static const char* TAG = "ota_valid";

// Единственный экземпляр адаптера для глобального доступа из других слоёв
// (NVS-заморозка D9/D10 через is_pending_global()).
OtaValidityAdapter* OtaValidityAdapter::instance_ = nullptr;

OtaValidityAdapter::OtaValidityAdapter()
{
    instance_ = this;

    running_ = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    // st читается только при ESP_OK — без отдельного инициализатора.
    pending_ = (esp_ota_get_state_partition(running_, &st) == ESP_OK &&
                st == ESP_OTA_IMG_PENDING_VERIFY);

    ESP_LOGI(TAG, "загружена партиция=%s pending_verify=%d",
             running_ ? running_->label : "?", pending_ ? 1 : 0);
}

OtaValidityAdapter::~OtaValidityAdapter()
{
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void OtaValidityAdapter::arm()
{
    if (armed_) {
        return;  // уже взведён — идемпотентно
    }
    armed_ = true;

    if (!pending_) {
        ESP_LOGI(TAG, "arm(): образ не pending — валидация не требуется");
        return;
    }

    // КРАШ-ПУТЬ: новая прошивка стартовала, но предыдущая загрузка была
    // крашем — откатываемся немедленно, без ожидания таймера.
    if (prev_boot_crash_) {
        rollback_and_reboot("предыдущая загрузка завершалась крашем");
        return;  // недостижимо — перезагрузка
    }

    deadline_us_ = esp_timer_get_time() + VALIDITY_WINDOW_US;
    ESP_LOGI(TAG, "arm(): взведён дедлайн валидации 90 с (healthy=HTTP вверх + цикл тикает)");
}

void OtaValidityAdapter::heartbeat()
{
    if (!pending_ || !armed_ || marked_valid_) {
        return;
    }

    // health = HTTP-сервер поднят + главный цикл тикает.
    // Сам факт вызова heartbeat() означает, что цикл жив — остаётся HTTP.
    const bool healthy = http_server_up_;
    if (healthy) {
        mark_valid();
        return;
    }

    // Здоровья пока нет — проверяем, не истёк ли 90-секундный дедлайн.
    if (esp_timer_get_time() >= deadline_us_) {
        rollback_and_reboot("истёк дедлайн 90 с, здоровье не подтверждено");
    }
}

void OtaValidityAdapter::mark_valid()
{
    if (marked_valid_) {
        return;
    }
    marked_valid_ = true;

    // Образ подтверждён валидным — PENDING_VERIFY снято. Это делает is_pending()
    // и is_pending_global() точными: NVS-заморозка (D9) и блокировка отката по
    // таймеру снимаются сразу после подтверждения, а не висят до конца сессии.
    // Логически помечаем валидным даже если вызов ниже не удастся (см. ниже).
    pending_ = false;

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Прошивка подтверждена валидной (mark_app_valid) — отмена отката");
    } else {
        // Помечаем как валидную логически, даже если вызов не удался —
        // повторных попыток не делаем, чтобы не зацикливаться.
        ESP_LOGE(TAG, "mark_app_valid_cancel_rollback вернул %s", esp_err_to_name(err));
    }
}

void OtaValidityAdapter::rollback_and_reboot(const char* reason)
{
    ESP_LOGE(TAG, "ОТКАТ прошивки: %s", reason);
    // esp_ota_mark_app_invalid_rollback_and_reboot() помечает партицию invalid
    // и перезагружает — при следующей загрузке bootloader уходит в прошлый слот.
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // Подстраховка: функция выше перезагружает и не возвращает управление,
    // но если по какой-то причине вернула — явная перезагрузка (партиция уже
    // помечена invalid, так что bootloader откатится).
    esp_restart();
}

void OtaValidityAdapter::mark_invalid_and_reboot()
{
    // IOtaValidity: ручной откат из UI — та же последовательность, что и авто-откат.
    rollback_and_reboot("ручной откат через UI");
}

bool OtaValidityAdapter::is_pending_global()
{
    return instance_ ? instance_->pending_ : false;
}
