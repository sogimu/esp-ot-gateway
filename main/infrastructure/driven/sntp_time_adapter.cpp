#include "infrastructure/driven/sntp_time_adapter.h"
#include "application/ports/driven/ilogger.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include <ctime>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SNTP_LED_GPIO GPIO_NUM_2

const char* SntpTimeAdapter::TAG = "sntp";

SntpTimeAdapter::SntpTimeAdapter(int tz_offset, bool has_internet, ILogger* log)
    : tz_offset_(tz_offset), logger_(log)
{
    strncpy(srv0_, "pool.ntp.org", sizeof(srv0_) - 1);
    strncpy(srv1_, "time.google.com", sizeof(srv1_) - 1);
    init();
    configure(tz_offset, has_internet);
}

SntpTimeAdapter::~SntpTimeAdapter()
{
    led_blink_stop();
    esp_sntp_stop();
}

// ── LED blink during SNTP sync ────────────────────────────

void SntpTimeAdapter::led_blink_cb(void* arg)
{
    auto* self = static_cast<SntpTimeAdapter*>(arg);
    self->blink_level_ = !self->blink_level_;
    gpio_set_level(SNTP_LED_GPIO, self->blink_level_);
}

void SntpTimeAdapter::led_blink_start() const
{
    if (!blink_timer_) {
        esp_timer_create_args_t args = {};
        args.callback = &SntpTimeAdapter::led_blink_cb;
        args.arg = const_cast<SntpTimeAdapter*>(this);
        args.name = "sntp_led";
        esp_timer_create(&args, &blink_timer_);
    }
    gpio_set_direction(SNTP_LED_GPIO, GPIO_MODE_OUTPUT);
    blink_level_ = 1;
    gpio_set_level(SNTP_LED_GPIO, 1);
    esp_timer_start_periodic(blink_timer_, 200000);  // 200 мс
}

void SntpTimeAdapter::led_blink_stop() const
{
    if (blink_timer_) {
        esp_timer_stop(blink_timer_);
        gpio_set_level(SNTP_LED_GPIO, 0);
    }
}

void SntpTimeAdapter::init()
{
    nvs_flash_init();
}

void SntpTimeAdapter::start()
{
    if (started_) return;
    started_ = true;

    // Start with UTC+0 so SNTP sets the system clock to correct UTC,
    // unaffected by any wrong timezone that may be stored in NVS.
    set_timezone(0);

    const char* s0 = srv0_[0] ? srv0_ : "pool.ntp.org";
    const char* s1 = srv1_[0] ? srv1_ : "";
    ESP_LOGI(TAG, "Инициализация SNTP (серверы: %s, %s)...", s0, s1);
    if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: запрос к %s, %s", s0, s1);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s0);
    esp_sntp_setservername(1, s1);
    esp_sntp_setservername(2, "216.239.35.0");  // Google NTP IP — bypass DNS
    esp_sntp_init();

    // Compute build timestamp once for validity checks
    time_t build_ts = 0;
    {
        struct tm build_tm = {};
        strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &build_tm);
        build_ts = mktime(&build_tm);
    }

    // If system clock already shows a time past the build timestamp,
    // the RTC retained a valid time from a previous sync — accept it.
    time_t now;
    std::time(&now);
    if (now > build_ts) {
        uint64_t real_us = (uint64_t)now * 1000000ULL;
        uint64_t boot_us = esp_timer_get_time();
        boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
        time_synced_ = true;
        ESP_LOGI(TAG, "SNTP: время действительно (сохранено с предыдущей синхр.)");
        return;
    }

    // Wait for first sync (max 30 seconds).
    ESP_LOGI(TAG, "Ожидание синхронизации SNTP...");

    // Blink onboard LED (GPIO2) while waiting for sync
    gpio_set_direction(SNTP_LED_GPIO, GPIO_MODE_OUTPUT);
    int led_state = 0;

    bool synced_in_loop = false;
    for (int i = 0; i < 600; i++) {
        if (i % 2 == 0) {
            led_state = !led_state;
            gpio_set_level(SNTP_LED_GPIO, led_state);
        }
        std::time(&now);
        // SNTP reports COMPLETED and time is post-2020 (plausible, not epoch)
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED
            && now >= 1577836800) {  // 2020-01-01 00:00:00 UTC
            gpio_set_level(SNTP_LED_GPIO, 0);  // LED off
            uint64_t real_us = (uint64_t)now * 1000000ULL;
            uint64_t boot_us = esp_timer_get_time();
            boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
            synced_in_loop = true;
            time_synced_ = true;
            ESP_LOGI(TAG, "SNTP синхронизирован через %s, UTC: %lld", s0, (long long)now);
            if (logger_) logger_->event(ILogger::SYSTEM,
                "SNTP: синхр. через %s, UNIX %lld с", s0, (long long)now);
            break;
        }
        // Diagnostic: log status every 5 seconds
        if (i > 0 && i % 100 == 0) {
            const char* st = "?";
            switch (sntp_get_sync_status()) {
            case SNTP_SYNC_STATUS_RESET:       st = "ожидание"; break;
            case SNTP_SYNC_STATUS_COMPLETED:   st = "синхр"; break;
            case SNTP_SYNC_STATUS_IN_PROGRESS: st = "плавная"; break;
            }
            ESP_LOGI(TAG, "SNTP: ожидание %d/%d с, статус=%s, RTC=%lld",
                     i / 20, 30, st, (long long)now);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!synced_in_loop) {
        gpio_set_level(SNTP_LED_GPIO, 0);  // LED off
        time_synced_ = false;
        ESP_LOGW(TAG, "SNTP: таймаут (30 с) — сервер %s недоступен", s0);
        if (logger_) logger_->event(ILogger::SYSTEM,
            "SNTP: таймаут, сервер %s недоступен", s0);

        // Try restoring manual time offset from NVS
        if (restore_time_offset()) {
            time_synced_ = true;
            ESP_LOGI(TAG, "SNTP недоступен — используется сохранённое время из NVS");
            if (logger_) logger_->event(ILogger::SYSTEM,
                "SNTP: недоступен, время восстановлено из NVS");
        } else {
            // Last resort: check if RTC retained a plausible time (post-2020)
            time_t rtc_now;
            std::time(&rtc_now);
            if (rtc_now >= 1577836800) {  // 2020-01-01
                uint64_t real_us = (uint64_t)rtc_now * 1000000ULL;
                uint64_t boot_us = esp_timer_get_time();
                boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
                ESP_LOGW(TAG, "SNTP недоступен — используется время RTC: %lld", (long long)rtc_now);
                if (logger_) logger_->event(ILogger::SYSTEM,
                    "SNTP: недоступен, время RTC: %lld с", (long long)rtc_now);
            } else {
                ESP_LOGW(TAG, "SNTP недоступен, время RTC недостоверно (%lld)", (long long)rtc_now);
                if (logger_) logger_->event(ILogger::SYSTEM,
                    "SNTP: недоступен, время RTC недостоверно");
            }
        }
    }
}

void SntpTimeAdapter::configure(int tz_offset, bool has_internet)
{
    set_timezone(tz_offset);
    if (has_internet) {
        start();
        set_timezone(tz_offset);  // восстановить — start() ставит UTC+0 для SNTP
        if (is_synced()) {
            save_time_offset();
        }
    } else {
        if (restore_time_offset()) {
            ESP_LOGI(TAG, "SNTP пропущен (нет STA) — время из NVS");
        } else {
            ESP_LOGW(TAG, "SNTP пропущен, ручное время не задано");
        }
    }
}

void SntpTimeAdapter::set_timezone(int tz)
{
    tz_offset_ = tz;
    char tz_str[32];
    // ESP-IDF newlib uses POSIX TZ format where the sign means WEST of UTC.
    // So UTC+7 (Tomsk) must be written as "UTC-7" (negative = east of UTC).
    snprintf(tz_str, sizeof(tz_str), "UTC%s%d", tz >= 0 ? "-" : "+", tz >= 0 ? tz : -tz);
    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс: UTC%+d (TZ=%s)", tz, tz_str);
}

void SntpTimeAdapter::set_servers(const char* srv0, const char* srv1)
{
    if (srv0 && srv0[0]) strncpy(srv0_, srv0, sizeof(srv0_) - 1);
    if (srv1 && srv1[0]) strncpy(srv1_, srv1, sizeof(srv1_) - 1);
}

uint64_t SntpTimeAdapter::monotonic_us() const
{
    return esp_timer_get_time();
}

ITimeSource::time_point SntpTimeAdapter::now() const
{
    auto since_boot = microseconds(esp_timer_get_time());
    if (boot_offset_us_.count() == 0) {
        return clock::from_time_t(::time(nullptr)) + (since_boot % seconds(1));
    }
    return time_point(since_boot + boot_offset_us_);
}

bool SntpTimeAdapter::is_synced() const
{
    // 1. Проверяем завершение SNTP. Логируем только первый раз —
    //    последующие вызовы с COMPLETED не дублируют сообщение.
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        if (!sntp_real_synced_) {
            led_blink_stop();  // синхронизация завершена — LED off
            time_t now;
            std::time(&now);
            if (now >= 1577836800) {
                uint64_t real_us = (uint64_t)now * 1000000ULL;
                uint64_t boot_us = esp_timer_get_time();
                boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
                time_synced_ = true;
                sntp_real_synced_ = true;
                const char* srv = srv0_[0] ? srv0_ : "pool.ntp.org";
                ESP_LOGI(TAG, "SNTP: синхронизирован через %s, UTC: %lld", srv, (long long)now);
                if (logger_) logger_->event(ILogger::SYSTEM,
                    "SNTP: синхр. через %s, UNIX %lld с", srv, (long long)now);
            }
        }
        return true;
    }

    // 2. SNTP не завершён. Retry — только если не было реальной синхронизации
    //    (время из NVS может быть устаревшим — продолжаем попытки).
    if (!sntp_real_synced_) {
        uint64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_sntp_retry_ms_ > 60000) {
            last_sntp_retry_ms_ = now_ms;
            led_blink_start();  // мигаем LED во время попытки синхронизации
            const char* srv = srv0_[0] ? srv0_ : "pool.ntp.org";
            ESP_LOGI(TAG, "SNTP: повторная попытка через %s...", srv);
            if (logger_) logger_->event(ILogger::SYSTEM,
                "SNTP: повторный запрос к %s", srv);
            sntp_restart();
        }
    }

    return time_synced_;
}

// ── Manual time ──────────────────────────────────────────

void SntpTimeAdapter::set_manual_time(time_t epoch_sec)
{
    uint64_t real_us = (uint64_t)epoch_sec * 1000000ULL;
    uint64_t boot_us = esp_timer_get_time();
    boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
    time_synced_ = true;
    ESP_LOGI(TAG, "Время установлено вручную: %lld", (long long)epoch_sec);
    if (logger_) logger_->event(ILogger::SYSTEM,
        "Время установлено вручную: %lld с", (long long)epoch_sec);
}

void SntpTimeAdapter::save_time_offset()
{
    int64_t offset = (int64_t)boot_offset_us_.count();
    if (offset == 0) return;  // don't save meaningless zero offset

    nvs_handle_t handle;
    if (nvs_open("config", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i64(handle, "utc_offset_us", offset);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Смещение времени сохранено в NVS: %lld us", (long long)offset);
    }
}

bool SntpTimeAdapter::restore_time_offset()
{
    nvs_handle_t handle;
    if (nvs_open("config", NVS_READONLY, &handle) != ESP_OK) return false;

    int64_t offset = 0;
    esp_err_t err = nvs_get_i64(handle, "utc_offset_us", &offset);
    nvs_close(handle);

    if (err == ESP_OK && offset != 0) {
        boot_offset_us_ = microseconds(offset);
        time_synced_ = true;
        ESP_LOGI(TAG, "Смещение времени загружено из NVS: %lld us", (long long)offset);
        return true;
    }
    return false;
}
