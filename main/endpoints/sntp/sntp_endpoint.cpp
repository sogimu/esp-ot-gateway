#include "sntp_endpoint.h"

#include "esp_sntp.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdlib>
#include <sys/time.h>

static const char* TAG = "sntp";

SntpEndpoint::SntpEndpoint()
    : started_(false)
    , tz_offset_(3)
{
}

SntpEndpoint::~SntpEndpoint()
{
    stop();
}

void SntpEndpoint::start()
{
    if (started_) return;

    ESP_LOGI(TAG, "Инициализация SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    started_ = true;

    set_timezone(tz_offset_);

    ESP_LOGI(TAG, "SNTP запущен, NTP серверы: pool.ntp.org, time.google.com");
}

void SntpEndpoint::stop()
{
    if (!started_) return;
    esp_sntp_stop();
    started_ = false;
    ESP_LOGI(TAG, "SNTP остановлен");
}

void SntpEndpoint::set_timezone(int offset_utc)
{
    tz_offset_ = offset_utc;
    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%+d", -offset_utc);
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс: UTC%+d", offset_utc);
}

bool SntpEndpoint::is_synced() const
{
    if (!started_) return false;
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    return (ti.tm_year >= (2024 - 1900));
}

bool SntpEndpoint::get_time(struct tm* out) const
{
    if (!started_ || !out) return false;
    time_t now;
    time(&now);
    localtime_r(&now, out);
    return (out->tm_year >= (2024 - 1900));
}