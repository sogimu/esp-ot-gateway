#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Глубина очереди команд MQTT
#define MQTT_QUEUE_DEPTH        8

/// Максимальная длина топика в элементе очереди
#define MQTT_QUEUE_TOPIC_MAX    64

/// Максимальная длина данных в элементе очереди
#define MQTT_QUEUE_PAYLOAD_MAX  512

/// Элемент очереди команд MQTT
typedef struct {
    char topic[MQTT_QUEUE_TOPIC_MAX];
    char payload[MQTT_QUEUE_PAYLOAD_MAX];
    int  payload_len;
} MqttQueueItem;

/// Непрозрачный дескриптор очереди
typedef void* MqttQueueHandle;

/// Создать очередь. Возвращает NULL при ошибке.
MqttQueueHandle mqtt_queue_create(void);

/// Удалить очередь.
void mqtt_queue_delete(MqttQueueHandle queue);

/// Положить элемент в очередь (неблокирующий вызов).
/// @return true если элемент помещён
/// @return false если очередь переполнена
bool mqtt_queue_push(MqttQueueHandle queue, const MqttQueueItem* item);

/// Извлечь элемент из очереди (неблокирующий вызов).
/// @param[out] item  Извлечённый элемент (только при возврате true)
/// @return true если элемент извлечён
/// @return false если очередь пуста
bool mqtt_queue_pop(MqttQueueHandle queue, MqttQueueItem* item);

#ifdef __cplusplus
}
#endif
