#pragma once

#include <stdint.h>

#define LOG_RING_SIZE 512

typedef enum {
    LOG_CAT_SYSTEM,
    LOG_CAT_USER,
    LOG_CAT_EQUIP,
    LOG_CAT_MODE
} LogCategory;

typedef struct {
    uint32_t time_sec;   /* Unix timestamp (NTP), 0 если время не известно */
    uint8_t  category;
    char     msg[48];
} LogEntry;

/* Вызывается из любого места для записи события */
void log_event(LogCategory cat, const char *fmt, ...);

/* Сериализация в JSON для /api/log.
   Возвращает указатель на статический буфер (перезаписывается при следующем вызове). */
const char *log_to_json(void);

/* Количество событий в буфере */
int log_count(void);
