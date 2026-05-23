#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static LogEntry s_ring[LOG_RING_SIZE];
static int      s_head   = 0;   /* следующий индекс для записи */
static int      s_count  = 0;   /* всего записей (до LOG_RING_SIZE) */

void log_event(LogCategory cat, const char *fmt, ...)
{
    int idx = s_head;
    s_head = (s_head + 1) % LOG_RING_SIZE;
    if (s_count < LOG_RING_SIZE) s_count++;

    time_t now;
    time(&now);
    s_ring[idx].time_sec = (uint32_t)(now > 0 ? now : 0);
    s_ring[idx].category = (uint8_t)cat;

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_ring[idx].msg, sizeof(s_ring[idx].msg), fmt, args);
    va_end(args);
}

int log_count(void)
{
    return s_count;
}

const char *log_to_json(void)
{
    static char buf[24576];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"count\":%d,\"events\":[", s_count);

    /* Элементы выдаём от старых к новым */
    int start = (s_count < LOG_RING_SIZE) ? 0 : s_head;
    int total = s_count;

    for (int i = 0; i < total && pos < (int)sizeof(buf) - 128; i++) {
        int idx = (start + i) % LOG_RING_SIZE;
        LogEntry *e = &s_ring[idx];

        struct tm ti;
        char tbuf[16] = "??:??:??";
        if (e->time_sec > 0) {
            time_t t = (time_t)e->time_sec;
            localtime_r(&t, &ti);
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
        }

        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"t\":\"%s\",\"c\":%d,\"m\":\"",
                        i ? "," : "", tbuf, e->category);

        /* Экранировать кавычки и бэкслеши в сообщении */
        for (const char *s = e->msg; *s && pos < (int)sizeof(buf) - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            buf[pos++] = *s;
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"}");
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    return buf;
}
