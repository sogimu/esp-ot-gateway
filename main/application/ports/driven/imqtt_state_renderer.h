#pragma once

#include <cstddef>

/// Интерфейс рендеринга JSON для MQTT-публикаций.
/// Выделен для тестирования MqttInteractor на хосте (WebPresenterAdapter
/// тянет зависимости от FreeRTOS через EventLogAdapter).
class IMqttStateRenderer {
public:
    virtual ~IMqttStateRenderer() = default;

    /// Отрендерить JSON состояния котла/отопления в buf.
    /// @return количество записанных байт (без учёта '\0')
    virtual int render_status(char* buf, size_t size) = 0;

    /// Отрендерить JSON статистики в buf.
    /// @return количество записанных байт (без учёта '\0')
    virtual int render_stats(char* buf, size_t size) = 0;
};
