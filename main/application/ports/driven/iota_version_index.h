#pragma once

#include <cstddef>

/// Driven-порт: каталог доступных версий прошивки (versions.json).
///
/// Конкретная реализация (OtaVersionIndexAdapter) тянет JSON с GitHub Pages
/// через esp_http_client + esp_crt_bundle и зависит от ESP-IDF. OtaInteractor
/// обращается к каталогу только через этот интерфейс → тестируется с fake.
class IOtaVersionIndex {
public:
    virtual ~IOtaVersionIndex() = default;

    /// Загрузить индекс версий.
    /// Возвращает NUL-terminated JSON в куче (вызывающий освобождает free()),
    /// либо nullptr при ошибке транспорта/пустом ответе.
    virtual char* fetch_versions() = 0;
};
