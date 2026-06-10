#include "infrastructure/driven/sntp_time_adapter.h"
#include "application/ports/driven/ilogger.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
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
            ESP_LOGI(TAG, "SNTP синхронизирован, UTC: %lld", (long long)now);
            if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: sync OK, UTC %lld",
                                        (long long)now);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        gpio_set_level(SNTP_LED_GPIO, 0);  // LED off
        ESP_LOGW(TAG, "SNTP: таймаут синхронизации");
        if (logger_) logger_->event(ILogger::SYSTEM, "SNTP: таймаут");
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

uint64_t SntpTimeAdapter::now_us() const
{
    return esp_timer_get_time();
}

uint32_t SntpTimeAdapter::now_sec() const
{
    time_t now;
    time(&now);
    return static_cast<uint32_t>(now > 0 ? now : 0);
}
