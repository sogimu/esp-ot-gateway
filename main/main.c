#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "opentherm.h"
#include "http_server.h"
#include "wifi_config.h"

static const char *TAG = "main";

/* ── WiFi ──────────────────────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "WiFi переподключение... попытка %d/%d",
                     s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi: не удалось подключиться");
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi подключён. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web интерфейс: http://" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,    wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Подключение к WiFi SSID: %s ...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) return true;

    ESP_LOGE(TAG, "WiFi не подключён — перезагрузка через 5 сек");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
    return false;
}

/* ── Состояние котла ────────────────────────────────────────────────────────  */

static OT_State boiler = {
    .ch_enable    = true,
    .dhw_enable   = true,
    .ch_setpoint  = 30.0f,
    .dhw_setpoint = 55.0f,
    .connected    = false,
};

/* ── Расписание отопления (24 часа) ────────────────────────────────────────  */

CH_Schedule g_schedule = {
    .enabled = false,
    .temps = {30,30,30,30,30,30, 35,40,40,35,35,35, 35,35,35,35,35,40, 40,40,40,35,35,30},
};

/* ── NTP ────────────────────────────────────────────────────────────────────  */

int g_tz_offset = 3;   /* часовой пояс UTC+N (по умолчанию MSK = +3) */

void apply_tz(void)
{
    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%+d", -g_tz_offset);  /* POSIX: UTC-3 = UTC+3 */
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "Часовой пояс: UTC%+d (%s)", g_tz_offset, tz);
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "Инициализация SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    apply_tz();
}

/* Получить текущий час (0-23), или -1 если время не синхронизировано */
int get_current_hour(void)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year < (2024 - 1900)) return -1;
    return ti.tm_hour;
}

/* Получить текущее время в формате "HH:MM:SS" (или "NTP..." если не синхронизировано) */
void get_current_time_str(char *buf, size_t len)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year < (2024 - 1900)) {
        snprintf(buf, len, "NTP...");
        return;
    }
    snprintf(buf, len, "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
}

/* ── Задача опроса котла ────────────────────────────────────────────────────  */
static void boiler_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Задача опроса котла запущена");

    while (1) {
        /* Применить расписание */
        if (g_schedule.enabled) {
            int h = get_current_hour();
            if (h >= 0 && h < 24) {
                boiler.ch_setpoint = g_schedule.temps[h];
            }
        }

        OT_Poll(&boiler);
        vTaskDelay(pdMS_TO_TICKS(OT_POLL_INTERVAL_MS));
    }
}

/* ── app_main ───────────────────────────────────────────────────────────────  */
void app_main(void)
{
    /* NVS — необходим для WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "=== Газовый котёл Baxi duo-tec compact ===");
    ESP_LOGI(TAG, "Прошивка: OpenTherm + WiFi + HTTP");

    /* WiFi */
    wifi_init_sta();

    /* NTP — синхронизация времени для расписания */
    init_sntp();

    /* OpenTherm */
    OT_Init();

    /* HTTP сервер */
    HTTP_Server_Start(&boiler);

    /* Задача опроса котла (отдельный стек, нормальный приоритет) */
    xTaskCreate(boiler_task, "boiler", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Система запущена");
}
