#include "infrastructure/driven/esp_ota_adapter.h"

#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

static const char* TAG = "esp_ota";

static constexpr const char* FIRMWARE_BASE_URL =
    "https://sogimu.github.io/esp-ot-gateway/firmware/";

EspOtaAdapter::EspOtaAdapter()
{
    err_lock_ = xSemaphoreCreateMutex();
}

EspOtaAdapter::~EspOtaAdapter()
{
    if (err_lock_) vSemaphoreDelete(err_lock_);
}

void EspOtaAdapter::set_last_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (err_lock_) xSemaphoreTake(err_lock_, portMAX_DELAY);
    vsnprintf(last_error_, sizeof(last_error_), fmt, args);
    if (err_lock_) xSemaphoreGive(err_lock_);
    va_end(args);
}

void EspOtaAdapter::copy_last_error(char* dst, size_t n) const
{
    if (dst == nullptr || n == 0) return;
    if (err_lock_) xSemaphoreTake(err_lock_, portMAX_DELAY);
    strncpy(dst, last_error_, n - 1);
    if (err_lock_) xSemaphoreGive(err_lock_);
    dst[n - 1] = '\0';
}

bool EspOtaAdapter::download(const char* tag)
{
    progress_pct_ = 0;
    last_error_[0] = '\0';

    if (tag == nullptr || tag[0] == '\0') {
        set_last_error("пустой тег версии");
        state_ = State::FAILED;
        return false;
    }

    char url[192];
    int n = snprintf(url, sizeof(url), "%s%s/esp-ot-gateway.bin",
                     FIRMWARE_BASE_URL, tag);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        set_last_error("слишком длинный URL для тега %s", tag);
        state_ = State::FAILED;
        return false;
    }

    ESP_LOGI(TAG, "OTA: загрузка %s (тег %s)", url, tag);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url               = url;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.timeout_ms        = 30000;
    http_cfg.keep_alive_enable = true;
    http_cfg.buffer_size       = 1024;
    http_cfg.buffer_size_tx    = 1024;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t h = nullptr;
    state_ = State::DOWNLOADING;

    esp_err_t err = esp_https_ota_begin(&ota_cfg, &h);
    if (err != ESP_OK) {
        set_last_error("begin: 0x%x", (unsigned)err);
        ESP_LOGE(TAG, "esp_https_ota_begin: 0x%x", (unsigned)err);
        state_ = State::FAILED;
        return false;
    }

    int image_size = esp_https_ota_get_image_size(h);

    // IDF-стандартный цикл: perform возвращает ESP_ERR_HTTPS_OTA_IN_PROGRESS
    // пока есть данные, и ESP_OK когда всё скачано и записано во flash.
    while (1) {
        err = esp_https_ota_perform(h);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int read = esp_https_ota_get_image_len_read(h);
        if (image_size > 0) {
            int pct = (int)((int64_t)read * 100 / image_size);
            if (pct > 100) pct = 100;
            if (pct < 0)   pct = 0;
            progress_pct_ = pct;
        }
    }

    if (err != ESP_OK) {
        set_last_error("perform: 0x%x", (unsigned)err);
        ESP_LOGE(TAG, "esp_https_ota_perform: 0x%x", (unsigned)err);
        esp_https_ota_abort(h);
        state_ = State::FAILED;
        return false;
    }

    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED)
            set_last_error("finish: образ повреждён (validate)");
        else
            set_last_error("finish: 0x%x", (unsigned)err);
        ESP_LOGE(TAG, "esp_https_ota_finish: 0x%x", (unsigned)err);
        state_ = State::FAILED;
        return false;
    }

    progress_pct_ = 100;
    state_ = State::DONE;
    ESP_LOGI(TAG, "OTA: образ записан, ожидает mark_valid (PENDING_VERIFY)");
    return true;
}
