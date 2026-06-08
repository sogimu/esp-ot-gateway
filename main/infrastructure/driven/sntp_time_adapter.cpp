#include "infrastructure/driven/sntp_time_adapter.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"

#include <ctime>
#include <cstring>

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

    ESP_LOGI(TAG, "Инициализация SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, srv0_);
    esp_sntp_setservername(1, srv1_);
    esp_sntp_init();

    set_timezone(tz_offset_);

    ESP_LOGI(TAG, "SNTP запущен, серверы: %s, %s", srv0_, srv1_);
}

void SntpTimeAdapter::set_timezone(int tz)
{
    tz_offset_ = tz;
    char tz_str[32];
    snprintf(tz_str, sizeof(tz_str), "UTC%s%d", tz >= 0 ? "+" : "", tz);
    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс: %s", tz_str);
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
