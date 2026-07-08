#include "infrastructure/freertos/mqtt_queue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

MqttQueueHandle mqtt_queue_create(void)
{
    return (MqttQueueHandle) xQueueCreate(MQTT_QUEUE_DEPTH, sizeof(MqttQueueItem));
}

void mqtt_queue_delete(MqttQueueHandle queue)
{
    if (queue) {
        vQueueDelete((QueueHandle_t)queue);
    }
}

bool mqtt_queue_push(MqttQueueHandle queue, const MqttQueueItem* item)
{
    if (!queue || !item) return false;
    BaseType_t ret = xQueueSend((QueueHandle_t)queue, item, 0);
    return (ret == pdPASS);
}

bool mqtt_queue_pop(MqttQueueHandle queue, MqttQueueItem* item)
{
    if (!queue || !item) return false;
    BaseType_t ret = xQueueReceive((QueueHandle_t)queue, item, 0);
    return (ret == pdPASS);
}
