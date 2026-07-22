#include "infrastructure/driven/ota_version_index_adapter.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include <cstdlib>
#include <cstring>

static const char* TAG = "ota_versions";

// URL индекса версий на GitHub Pages (НЕ GitHub Releases API — без rate limit).
static constexpr const char* VERSIONS_URL =
    "https://sogimu.github.io/esp-ot-gateway/versions.json";

// Параметры чтения.
static constexpr int    HTTP_TIMEOUT_MS  = 15000;       // таймаут ~15 с
static constexpr int    HTTP_BUFFER_SIZE = 1024;        // приёмный буфер клиента/чанка
static constexpr size_t INITIAL_BODY_CAP = 2048;        // стартовая ёмкость тела
static constexpr size_t MAX_BODY_SIZE    = 64 * 1024;   // защита от бесконтрольного роста

char* OtaVersionIndexAdapter::fetch_versions()
{
    ESP_LOGI(TAG, "загрузка индекса версий: %s", VERSIONS_URL);

    // Конфигурация HTTP-клиента: HTTPS, сертификат сервера проверяется через
    // встроенный crt_bundle (cert_pem = NULL), таймаут 15 c, keep-alive,
    // приёмный и передающий буферы по 1 КБ.
    esp_http_client_config_t cfg = {};
    cfg.url               = VERSIONS_URL;
    cfg.cert_pem          = nullptr;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = HTTP_TIMEOUT_MS;
    cfg.keep_alive_enable = true;
    cfg.buffer_size       = HTTP_BUFFER_SIZE;
    cfg.buffer_size_tx    = HTTP_BUFFER_SIZE;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        ESP_LOGE(TAG, "esp_http_client_init вернул NULL");
        return nullptr;
    }

    // Динамический буфер под тело ответа.
    size_t cap  = INITIAL_BODY_CAP;
    size_t len  = 0;
    char*  body = static_cast<char*>(calloc(cap, 1));
    if (body == nullptr) {
        ESP_LOGE(TAG, "нет памяти под тело (%u байт)", (unsigned)cap);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    // Открываем GET-запрос (длина тела запроса = 0).
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: 0x%x", (unsigned)err);
        free(body);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    // Читаем заголовки ответа (статус, Content-Length). При chunked-ответе
    // content_length будет -1 — это нормально, читаем до EOF.
    /*int content_length =*/ esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP-статус %d (ожидали 200)", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(body);
        return nullptr;
    }

    // Читаем тело чанками в динамический буфер.
    char chunk[HTTP_BUFFER_SIZE];
    while (true) {
        int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n == -ESP_ERR_HTTP_EAGAIN) {
            // Транзитный случай в неблокирующем режиме; повторяем попытку
            // чтения (ограничено общим таймаутом клиента).
            continue;
        }
        if (n < 0) {
            ESP_LOGE(TAG, "read: 0x%x", (unsigned)n);
            free(body);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return nullptr;
        }
        if (n == 0) {
            // EOF — тело целиком прочитано.
            break;
        }

        // При необходимости растим буфер так, чтобы поместились n байт и NUL.
        if (len + (size_t)n + 1 > MAX_BODY_SIZE) {
            ESP_LOGE(TAG, "тело превысило лимит %u байт", (unsigned)MAX_BODY_SIZE);
            free(body);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return nullptr;
        }
        if (len + (size_t)n + 1 > cap) {
            size_t new_cap = cap;
            while (len + (size_t)n + 1 > new_cap) {
                new_cap *= 2;
            }
            char* grown = static_cast<char*>(realloc(body, new_cap));
            if (grown == nullptr) {
                ESP_LOGE(TAG, "нет памяти при росте буфера до %u", (unsigned)new_cap);
                free(body);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return nullptr;
            }
            body = grown;
            // Обнуляем новую область (включая будущий NUL-терминатор).
            memset(body + cap, 0, new_cap - cap);
            cap = new_cap;
        }

        memcpy(body + len, chunk, (size_t)n);
        len += (size_t)n;
    }

    int64_t reported_len = esp_http_client_get_content_length(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (len == 0) {
        ESP_LOGE(TAG, "пустое тело ответа");
        free(body);
        return nullptr;
    }

    // Гарантируем NUL-terminator (calloc/memset уже дают его, но
    // удостоверяемся явно).
    body[len] = '\0';

    ESP_LOGI(TAG, "индекс версий получен: %u байт, HTTP %d, content-length=%lld",
             (unsigned)len, status, (long long)reported_len);

    return body;
}
