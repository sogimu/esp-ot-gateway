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
    strncpy(srv0_, "pool.ntp.org", sizeof(srv0_) - 1);
    strncpy(srv1_, "time.google.com", sizeof(srv1_) - 1);
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
    if (srv0_[0]) esp_sntp_setservername(0, srv0_);
    else esp_sntp_setservername(0, "pool.ntp.org");
    if (srv1_[0]) esp_sntp_setservername(1, srv1_);
    else esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    started_ = true;

    set_timezone(tz_offset_);

    ESP_LOGI(TAG, "SNTP запущен, NTP серверы: %s, %s", srv0_, srv1_);
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

void SntpEndpoint::set_servers(const char* srv0, const char* srv1)
{
    strncpy(srv0_, srv0, sizeof(srv0_) - 1);
    strncpy(srv1_, srv1, sizeof(srv1_) - 1);
    if (started_) {
        esp_sntp_stop();
        started_ = false;
        start();
    }
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