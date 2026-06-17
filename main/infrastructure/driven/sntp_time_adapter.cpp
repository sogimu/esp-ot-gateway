#include "infrastructure/driven/sntp_time_adapter.h"
#include "application/ports/driven/ilogger.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "driver/gpio.h"

#include <ctime>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SNTP_LED_GPIO GPIO_NUM_2

const char* SntpTimeAdapter::TAG = "sntp";

SntpTimeAdapter::SntpTimeAdapter()
{
    strncpy(srv0_, "pool.ntp.org", sizeof(srv0_) - 1);
    strncpy(srv1_, "time.google.com", sizeof(srv1_) - 1);
}

SntpTimeAdapter::~SntpTimeAdapter()
{
    esp_sntp_stop();
}

void SntpTimeAdapter::start()
{
    if (started_) return;
    started_ = true;

    // Start with UTC+0 so SNTP sets the system clock to correct UTC,
    // unaffected by any wrong timezone that may be stored in NVS.
    set_timezone(0);

    ESP_LOGI(TAG, "Инициализация SNTP (серверы: %s, %s)...", srv0_, srv1_);
    if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: запрос к %s", srv0_);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, srv0_);
    esp_sntp_setservername(1, srv1_);
    esp_sntp_init();

    // Wait for first sync (max 15 seconds). The RTC may retain a wrong
    // time from a previous boot, so sntp_get_sync_status() alone is not
    // enough — we also verify the time is >= build timestamp, meaning SNTP
    // has actually adjusted the clock forward from the retained value.
    ESP_LOGI(TAG, "Ожидание синхронизации SNTP...");

    // Blink onboard LED (GPIO2) while waiting for sync
    gpio_set_direction(SNTP_LED_GPIO, GPIO_MODE_OUTPUT);
    int led_state = 0;

    time_t build_ts = 0;
    {
        struct tm build_tm = {};
        strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &build_tm);
        build_ts = mktime(&build_tm);
    }
    for (int i = 0; i < 600; i++) {
        // Toggle LED every 100ms (every 2 iterations of 50ms)
        if (i % 2 == 0) {
            led_state = !led_state;
            gpio_set_level(SNTP_LED_GPIO, led_state);
        }
        time_t now;
        std::time(&now);
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            gpio_set_level(SNTP_LED_GPIO, 0);  // LED off
            // Compute offset from boot time to real Unix epoch
            uint64_t real_us = (uint64_t)now * 1000000ULL;
            uint64_t boot_us = esp_timer_get_time();
            boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
            ESP_LOGI(TAG, "SNTP синхронизирован, UTC: %lld", (long long)now);
            if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: синхр. OK, UNIX-время %lld с",
                                        (long long)now);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        gpio_set_level(SNTP_LED_GPIO, 0);  // LED off
        sntp_synced_ = false;
        ESP_LOGW(TAG, "SNTP: таймаут синхронизации");
        if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: таймаут");

        // Try restoring manual time offset from NVS
        if (restore_time_offset()) {
            ESP_LOGI(TAG, "SNTP недоступен — используется сохранённое время");
            if (logger_) logger_->event(ILogger::SYSTEM,
                "SNTP: недоступен, время из NVS");
        } else {
            ESP_LOGW(TAG, "SNTP недоступен, ручное время не задано");
            if (logger_) logger_->event(ILogger::SYSTEM,
                "SNTP: недоступен, время не задано");
        }
    } else {
        sntp_synced_ = true;
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

// ── Manual time ──────────────────────────────────────────

void SntpTimeAdapter::set_manual_time(time_t epoch_sec)
{
    uint64_t real_us = (uint64_t)epoch_sec * 1000000ULL;
    uint64_t boot_us = esp_timer_get_time();
    boot_offset_us_ = microseconds((int64_t)(real_us - boot_us));
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
        ESP_LOGI(TAG, "Смещение времени загружено из NVS: %lld us", (long long)offset);
        return true;
    }
    return false;
}
