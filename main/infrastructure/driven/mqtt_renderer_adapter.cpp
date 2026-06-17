#include "infrastructure/driven/mqtt_renderer_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"

int MqttRendererAdapter::render_status(char* buf, size_t size)
{
    return web_.render_status(buf, size);
}

int MqttRendererAdapter::render_stats(char* buf, size_t size)
{
    return web_.render_stats(buf, size);
}
