#pragma once

#include <cstdint>

#include "application/ports/driven/iota_version_index.h"

/// Адаптер индекса версий OTA: скачивает versions.json с GitHub Pages и
/// отдаёт тело ответа как NUL-terminated char* в куче. Вызывающий
/// освобождает возвращённый указатель через free().
///
/// Источник: https://sogimu.github.io/esp-ot-gateway/versions.json
/// (GitHub Pages, НЕ GitHub Releases API — нет лимитов на запросы).
/// Реализация через esp_http_client + esp_crt_bundle_attach, таймаут ~15 с,
/// чтение тела чанками в динамический буфер (calloc/realloc).
///
/// При ошибке транспорта (нет сети, DNS, TLS, HTTP-статус != 200,
/// пустое тело, превышение лимита размера) метод возвращает nullptr —
/// без краха устройства.
class OtaVersionIndexAdapter : public IOtaVersionIndex {
public:
    OtaVersionIndexAdapter() = default;

    /// Загрузить versions.json.
    /// Возвращает NUL-terminated JSON в куче (освобождается free()),
    /// либо nullptr при ошибке транспорта/разборе ответа.
    char* fetch_versions() override;
};
