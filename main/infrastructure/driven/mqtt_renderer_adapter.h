#pragma once

#include "application/ports/driven/imqtt_state_renderer.h"

class WebPresenterAdapter;

/// Адаптер: IMqttStateRenderer поверх WebPresenterAdapter.
/// Используется для подключения MqttInteractor к существующему WebPresenterAdapter.
class MqttRendererAdapter : public IMqttStateRenderer {
public:
    explicit MqttRendererAdapter(WebPresenterAdapter& web) : web_(web) {}

    int render_status(char* buf, size_t size) override;
    int render_stats(char* buf, size_t size) override;

private:
    WebPresenterAdapter& web_;
};
