#pragma once

#include "opentherm.h"
#include "esp_http_server.h"

/* Расписание отопления — 24 часовых слота */
typedef struct {
    bool  enabled;
    float temps[24];   /* уставка CH для каждого часа 0..23 */
} CH_Schedule;

/* Глобальные переменные (определены в main.c) */
extern CH_Schedule g_schedule;
extern int get_current_hour(void);
extern void get_current_time_str(char *buf, size_t len);
extern int g_tz_offset;
extern void apply_tz(void);

/* Запустить HTTP-сервер на порту 80.
   state — указатель на глобальное состояние котла. */
httpd_handle_t HTTP_Server_Start(OT_State *state);
